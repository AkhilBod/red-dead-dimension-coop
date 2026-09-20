#include "FGTrainPlayer.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "FGAssets.h"
#include "FGIronHorseGameMode.h"
#include "FGTrackerInput.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

namespace
{
    const FFGWeapon Weapons[] = {
        //  name            prop                      len  rounds  cooldown  damage  hits  assist  tracers  pitch
        { TEXT("REVOLVER"),   nullptr,                   0.0f, 6, 0.25f, 25.0f, 1, 1.0f, 1, 1.00f },
        { TEXT("SHOTGUN"),    TEXT("SM_Shotgun"),       87.0f, 4, 0.55f, 50.0f, 3, 1.7f, 6, 0.72f },   // wide, takes a cluster, slow, four shells
        { TEXT("RIFLE"),      TEXT("SM_Rifle"),        100.0f, 8, 0.35f, 50.0f, 2, 1.0f, 1, 1.25f },   // drops a heavy in one, goes through to the man behind
        { TEXT("LONG COLT"),  TEXT("SM_Revolver_Long"), 44.0f, 7, 0.20f, 25.0f, 1, 1.15f, 1, 1.10f },  // quick, seven rounds
    };
}

const FFGWeapon& AFGTrainPlayer::Weapon() const { return Weapons[WeaponIndex]; }

void AFGTrainPlayer::SetWeapon(int32 Index)
{
    WeaponIndex = ((Index % int32(UE_ARRAY_COUNT(Weapons))) + UE_ARRAY_COUNT(Weapons)) % UE_ARRAY_COUNT(Weapons);
    const FFGWeapon& W = Weapon();
    MagazineSize = W.Rounds;
    CurrentAmmo = W.Rounds;
    FireCooldown = W.Cooldown;
    ShotDamage = W.Damage;
    // The viewmodel is one skinned mesh, arm and revolver together, so a long gun replaces all of it.
    const bool bProp = W.Prop != nullptr;
    LongGun->SetStaticMesh(bProp ? FGAssets::StaticMesh(TEXT("props"), W.Prop) : nullptr);
    LongGun->SetVisibility(bProp);
    Revolver->SetVisibility(!bProp, false);
    if (bProp)
    {
        // Both are modelled pointing along +Y with the origin at the grip. Put the prop's grip where the revolver's is:
        // its muzzle bone, less the revolver's own barrel (37 cm).
        const FVector Muzzle = Revolver->DoesSocketExist(TEXT("muzzle")) ? Revolver->GetSocketTransform(TEXT("muzzle"), RTS_Component).GetLocation() : FVector(7.0f, 30.0f, -8.0f);
        LongGun->SetRelativeLocation(Muzzle - FVector(0.0f, 37.0f, 3.0f));
    }
}

AFGTrainPlayer::AFGTrainPlayer()
{
    Tracker = CreateDefaultSubobject<UFGTrackerInput>(TEXT("Tracker"));

    CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
    CameraArm->SetupAttachment(GetCapsuleComponent());
    CameraArm->SetRelativeLocation(FVector(0.0f, 0.0f, StandingCameraZ));
    CameraArm->TargetArmLength = 1.0f;          // zero would switch the collision sweep off
    CameraArm->bDoCollisionTest = true;
    CameraArm->ProbeSize = 14.0f;
    CameraArm->ProbeChannel = ECC_Camera;
    CameraArm->bUsePawnControlRotation = false;
    CameraArm->bEnableCameraLag = false;
    FirstPersonCamera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
    FirstPersonCamera->SetRelativeLocation(FVector::ZeroVector);

    Revolver = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Revolver"));
    Revolver->SetupAttachment(FirstPersonCamera);
    Revolver->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Revolver->SetCastShadow(false);
    Revolver->bOnlyOwnerSee = false;

    LongGun = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LongGun"));
    LongGun->SetupAttachment(Revolver);
    LongGun->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    LongGun->SetCastShadow(false);
    LongGun->SetVisibility(false);

    RevolverL = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("RevolverL"));
    RevolverL->SetupAttachment(FirstPersonCamera);
    RevolverL->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RevolverL->SetCastShadow(false);
    RevolverL->SetRelativeScale3D(FVector(-1.0f, 1.0f, 1.0f));      // mirrored: the materials are two-sided

    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FirstPersonCamera->SetFieldOfView(80.0f);

    // PLAN.md: lean is about a metre sideways, a duck about 0.8 m down.
    LeanDistance = 100.0f;
    // The base class ducks by moving the camera on the capsule AND shrinking the capsule, which lowers the whole actor:
    // 128 cm in all, which put the view inside the car. Both are neutralised here and the arm does the duck instead.
    StandingCameraZ = 0.0f;
    CrouchedCameraZ = 0.0f;
    CrosshairScreenMargin = 0.0f;   // the tracker's 0..1 already means the whole screen
    bDrawShotDebug = false;
    AutoPossessPlayer = EAutoReceiveInput::Disabled;
}

void AFGTrainPlayer::BeginPlay()
{
    Super::BeginPlay();
    CrouchedCapsuleHalfHeight = StandingCapsuleHalfHeight;
    Revolver->SetSkeletalMesh(FGAssets::SkeletalMesh(TEXT("fx"), TEXT("SK_PlayerRevolver")));
    RevolverL->SetSkeletalMesh(FGAssets::SkeletalMesh(TEXT("fx"), TEXT("SK_PlayerRevolver")));
    PlayGun(TEXT("fp_idle"), true);
    PlayGun(TEXT("fp_idle"), true, 1);
    AmmoL = MagazineSize;
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

USkeletalMeshComponent* AFGTrainPlayer::ModelFor(int32 Gun) const
{
    // One gun: always the right-hand model. Two: each hand gets the model on its own side of the picture.
    const bool bOnRight = !IsDual() || ((Gun == 0) == Tracker->State.bPrimaryOnRight);
    return bOnRight ? Revolver : RevolverL;
}

bool AFGTrainPlayer::IsEmpty() const
{
    return CurrentAmmo <= 0 && (!IsDual() || AmmoL <= 0);
}

void AFGTrainPlayer::PlayGun(const FString& Action, bool bLoop, int32 Gun)
{
    if (UAnimSequence* Seq = FGAssets::Anim(TEXT("fx"), TEXT("SK_PlayerRevolver"), Action))
    {
        ModelFor(Gun)->PlayAnimation(Seq, bLoop);
    }
}

void AFGTrainPlayer::PoseGun(USkeletalMeshComponent* Model, bool bLeft, FVector2D Aim, float Down, float Kick)
{
    // The gun follows its crosshair about half way, so it looks pointed without covering the target.
    const float HalfFov = FirstPersonCamera->FieldOfView * 0.5f;
    const float Yaw = (Aim.X - 0.5f) * 2.0f * HalfFov * 0.55f;
    const float Pitch = (0.5f - Aim.Y) * 2.0f * HalfFov * 0.5625f * 0.55f;
    const float Side = bLeft ? -1.0f : 1.0f;
    Model->SetRelativeLocation(FVector(38.0f - Kick * 6.0f, Side * 15.0f + (Aim.X - 0.5f) * 14.0f, -17.0f - Down * 45.0f));
    Model->SetRelativeRotation(FRotator(Pitch + Kick * 14.0f - Down * 50.0f, Yaw - 90.0f, 0.0f));
}

void AFGTrainPlayer::Tick(float DeltaTime)
{
    const FFGTrackerState& In = Tracker->State;
    SetBodyInput(In.Lean, 1.0f - In.Duck);
    SetAimNormalized(In.AimX * 2.0f - 1.0f, 1.0f - In.AimY * 2.0f);
    Super::Tick(DeltaTime);
    ForcedDropNow = FMath::FInterpTo(ForcedDropNow, ForcedDropCm, DeltaTime, 9.0f);
    CameraArm->SocketOffset = FVector(0.0f, 0.0f, -FMath::Max(DuckDropCm * In.Duck, ForcedDropNow));

    HitFlash = FMath::Max(0.0f, HitFlash - DeltaTime * 1.2f);
    Recoil = FMath::FInterpTo(Recoil, 0.0f, DeltaTime, 9.0f);

    // Train rumble and sway: a little roll and bob that grows with speed.
    const float T = GetWorld()->GetTimeSeconds();
    const FRotator Sway(
        FMath::Sin(T * 7.3f) * 0.18f * Rumble + Recoil * 1.6f,
        FMath::Sin(T * 0.9f) * 0.35f * Rumble,
        FMath::Sin(T * 1.7f) * 0.7f * Rumble + In.Lean * 2.5f);
    FirstPersonCamera->SetRelativeRotation(CameraBaseRotation + Sway);

    RecoilL = FMath::FInterpTo(RecoilL, 0.0f, DeltaTime, 9.0f);
    const bool bDown = In.bHolstered || !In.bAimValid || bGunHidden;
    HolsterBlend = FMath::FInterpTo(HolsterBlend, bDown ? 1.0f : 0.0f, DeltaTime, 10.0f);
    DualBlend = FMath::FInterpTo(DualBlend, In.bAim2Valid && !bGunHidden ? 1.0f : 0.0f, DeltaTime, 8.0f);
    const FVector2D Aim1(In.AimX, In.AimY), Aim2(In.Aim2X, In.Aim2Y);
    const bool bSwap = IsDual() && !In.bPrimaryOnRight;
    PoseGun(Revolver, false, bSwap ? Aim2 : Aim1, bSwap ? 1.0f - DualBlend : HolsterBlend, bSwap ? RecoilL : Recoil);
    PoseGun(RevolverL, true, bSwap ? Aim1 : Aim2, bSwap ? HolsterBlend : 1.0f - DualBlend, bSwap ? Recoil : RecoilL);
    RevolverL->SetVisibility(DualBlend > 0.02f);
}

void AFGTrainPlayer::HandleFire(FVector2D Aim, int32 Gun)
{
    AFGIronHorseGameMode* GM = Game();
    APlayerController* PC = Cast<APlayerController>(GetController());
    if (!GM || !PC || !GM->PlayerMayFire())
    {
        return;
    }
    if (GM->HandleUiShot(Aim))
    {
        return;         // result screen: shots only press buttons
    }
    USkeletalMeshComponent* Model = ModelFor(Gun);
    FVector Origin, Dir;
    if (Gun == 0)
    {
        // The tracker rewinds the aim to where the hand pointed before the trigger motion. Shoot there.
        SetAimNormalized(Aim.X * 2.0f - 1.0f, 1.0f - Aim.Y * 2.0f);
        UpdateCrosshairPosition();
        if (!bInfiniteAmmo && CurrentAmmo <= 0)
        {
            Fire();     // raises Lohith's OnDryFire
            PlayGun(TEXT("fp_dry_fire"), false, 0);
            GM->PlaySfx(TEXT("dry"));
            return;
        }
        if (!CanFire() || !GetCrosshairWorldRay(Origin, Dir))
        {
            return;
        }
        Fire();         // Lohith: ammo, cooldown, world hitscan and damage
        Recoil = 1.0f;
    }
    else
    {
        // The second gun keeps its own six rounds and cooldown beside the base class's.
        int32 W = 0, H = 0;
        PC->GetViewportSize(W, H);
        const float Now = GetWorld()->GetTimeSeconds();
        if (AmmoL <= 0)
        {
            PlayGun(TEXT("fp_dry_fire"), false, 1);
            GM->PlaySfx(TEXT("dry"));
            return;
        }
        if (Now - LastFireL < FireCooldown || W <= 0 || !PC->DeprojectScreenPositionToWorld(Aim.X * W, Aim.Y * H, Origin, Dir))
        {
            return;
        }
        LastFireL = Now;
        --AmmoL;
        RecoilL = 1.0f;
    }
    ++ShotsFired;
    PlayGun(TEXT("fp_fire"), false, Gun);
    FVector Muzzle = Model->DoesSocketExist(TEXT("muzzle")) ? Model->GetSocketLocation(TEXT("muzzle")) : HeadLocation() + Dir * 60.0f;
    if (Gun == 0 && Weapon().Prop)
    {
        Muzzle = LongGun->GetComponentTransform().TransformPosition(FVector(0.0f, Weapon().PropLengthCm, 4.0f));
    }
    if (GM->ResolvePlayerShot(Origin, Dir.GetSafeNormal(), Muzzle))
    {
        ++ShotsHit;
    }
}

void AFGTrainPlayer::HandleReload()
{
    AFGIronHorseGameMode* GM = Game();
    if (!GM || (CurrentAmmo >= MagazineSize && (!IsDual() || AmmoL >= MagazineSize)))
    {
        return;
    }
    Reload();
    AmmoL = MagazineSize;
    PlayGun(TEXT("fp_reload"), false, 0);
    if (IsDual()) { PlayGun(TEXT("fp_reload"), false, 1); }
    GM->PlaySfx(TEXT("reload"));
    GM->OnPlayerReloaded();
}

bool AFGTrainPlayer::TakeHit()
{
    Hats = FMath::Max(0, Hats - 1);
    HitFlash = 1.0f;
    return Hats <= 0;
}
