#include "FGTrainPlayer.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "FGAssets.h"
#include "FGIronHorseGameMode.h"
#include "FGTrackerInput.h"
#include "GameFramework/PlayerController.h"

AFGTrainPlayer::AFGTrainPlayer()
{
    Tracker = CreateDefaultSubobject<UFGTrackerInput>(TEXT("Tracker"));

    Revolver = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Revolver"));
    Revolver->SetupAttachment(FirstPersonCamera);
    Revolver->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Revolver->SetCastShadow(false);
    Revolver->bOnlyOwnerSee = false;

    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FirstPersonCamera->SetFieldOfView(80.0f);

    // PLAN.md: lean is about a metre sideways, a duck about 0.8 m down.
    LeanDistance = 100.0f;
    CrouchedCameraZ = -16.0f;
    CrosshairScreenMargin = 0.0f;   // the tracker's 0..1 already means the whole screen
    bDrawShotDebug = false;
    AutoPossessPlayer = EAutoReceiveInput::Disabled;
}

void AFGTrainPlayer::BeginPlay()
{
    Super::BeginPlay();
    Revolver->SetSkeletalMesh(FGAssets::SkeletalMesh(TEXT("fx"), TEXT("SK_PlayerRevolver")));
    PlayGun(TEXT("fp_idle"), true);
    Tracker->OnFire.AddUObject(this, &AFGTrainPlayer::HandleFire);
    Tracker->OnReload.AddUObject(this, &AFGTrainPlayer::HandleReload);
    CameraBaseRotation = FirstPersonCamera->GetRelativeRotation();

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        PC->bShowMouseCursor = true;
        PC->CurrentMouseCursor = EMouseCursor::Crosshairs;
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        PC->SetInputMode(Mode);
    }
}

AFGIronHorseGameMode* AFGTrainPlayer::Game() const
{
    return GetWorld() ? Cast<AFGIronHorseGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
}

FVector AFGTrainPlayer::HeadLocation() const
{
    return FirstPersonCamera->GetComponentLocation();
}

void AFGTrainPlayer::PlayGun(const FString& Action, bool bLoop)
{
    if (UAnimSequence* Seq = FGAssets::Anim(TEXT("fx"), TEXT("SK_PlayerRevolver"), Action))
    {
        Revolver->PlayAnimation(Seq, bLoop);
    }
}

void AFGTrainPlayer::Tick(float DeltaTime)
{
    const FFGTrackerState& In = Tracker->State;
    SetBodyInput(In.Lean, 1.0f - In.Duck);
    SetAimNormalized(In.AimX * 2.0f - 1.0f, 1.0f - In.AimY * 2.0f);
    Super::Tick(DeltaTime);

    HitFlash = FMath::Max(0.0f, HitFlash - DeltaTime * 1.2f);
    Recoil = FMath::FInterpTo(Recoil, 0.0f, DeltaTime, 9.0f);

    // Train rumble and sway: a little roll and bob that grows with speed.
    const float T = GetWorld()->GetTimeSeconds();
    const FRotator Sway(
        FMath::Sin(T * 7.3f) * 0.18f * Rumble + Recoil * 1.6f,
        FMath::Sin(T * 0.9f) * 0.35f * Rumble,
        FMath::Sin(T * 1.7f) * 0.7f * Rumble + In.Lean * 2.5f);
    FirstPersonCamera->SetRelativeRotation(CameraBaseRotation + Sway);

    // The gun follows the crosshair about half way, so it looks pointed without covering the target.
    const bool bDown = In.bHolstered || !In.bAimValid || bGunHidden;
    HolsterBlend = FMath::FInterpTo(HolsterBlend, bDown ? 1.0f : 0.0f, DeltaTime, 10.0f);
    const float HalfFov = FirstPersonCamera->FieldOfView * 0.5f;
    const float Yaw = (In.AimX - 0.5f) * 2.0f * HalfFov * 0.55f;
    const float Pitch = (0.5f - In.AimY) * 2.0f * HalfFov * 0.5625f * 0.55f;
    Revolver->SetRelativeLocation(FVector(38.0f - Recoil * 6.0f, 15.0f + (In.AimX - 0.5f) * 14.0f, -17.0f - HolsterBlend * 45.0f));
    Revolver->SetRelativeRotation(FRotator(Pitch + Recoil * 14.0f - HolsterBlend * 50.0f, Yaw - 90.0f, 0.0f));
}

void AFGTrainPlayer::HandleFire(FVector2D Aim)
{
    AFGIronHorseGameMode* GM = Game();
    if (!GM || !GM->PlayerMayFire())
    {
        return;
    }
    // The tracker rewinds the aim to where the hand pointed before the trigger motion. Shoot there.
    SetAimNormalized(Aim.X * 2.0f - 1.0f, 1.0f - Aim.Y * 2.0f);
    UpdateCrosshairPosition();

    if (GM->HandleUiShot(Aim))
    {
        return;         // result screen: shots only press buttons
    }
    if (!bInfiniteAmmo && CurrentAmmo <= 0)
    {
        Fire();         // raises Lohith's OnDryFire
        PlayGun(TEXT("fp_dry_fire"));
        GM->PlaySfx(TEXT("dry"));
        GM->OnPlayerDryFire();
        return;
    }
    if (!CanFire())
    {
        return;
    }
    FVector Origin, Dir;
    if (!GetCrosshairWorldRay(Origin, Dir))
    {
        return;
    }
    Fire();             // Lohith: ammo, cooldown, world hitscan and damage
    ++ShotsFired;
    Recoil = 1.0f;
    PlayGun(TEXT("fp_fire"));
    const FVector Muzzle = Revolver->DoesSocketExist(TEXT("muzzle")) ? Revolver->GetSocketLocation(TEXT("muzzle")) : HeadLocation() + Dir * 60.0f;
    if (GM->ResolvePlayerShot(Origin, Dir, Muzzle))
    {
        ++ShotsHit;
    }
}

void AFGTrainPlayer::HandleReload()
{
    AFGIronHorseGameMode* GM = Game();
    if (!GM || CurrentAmmo >= MagazineSize)
    {
        return;
    }
    Reload();
    PlayGun(TEXT("fp_reload"));
    GM->PlaySfx(TEXT("reload"));
    GM->OnPlayerReloaded();
}

bool AFGTrainPlayer::TakeHit()
{
    Hats = FMath::Max(0, Hats - 1);
    HitFlash = 1.0f;
    return Hats <= 0;
}
