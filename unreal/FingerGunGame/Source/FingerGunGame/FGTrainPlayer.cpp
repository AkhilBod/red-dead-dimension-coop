#include "FGTrainPlayer.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "FGAssets.h"
#include "FGBandit.h"
#include "FGCombat.h"
#include "FGGameState.h"
#include "FGIronHorseGameMode.h"
#include "FGPlayerController.h"
#include "FGPlayerState.h"
#include "FGPresentation.h"
#include "FGTarget.h"
#include "FGWorldStreamer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

namespace
{
    const FFGWeapon Weapons[] = {
        //  name            prop                      len  rounds  cooldown  damage  hits  assist  tracers  pitch
        { TEXT("REVOLVER"),   nullptr,                   0.0f, 6, 0.25f, 25.0f, 1, 1.0f, 1, 1.00f },
        { TEXT("SHOTGUN"),    TEXT("SM_Shotgun"),       87.0f, 2, 0.45f, 50.0f, 6, 2.8f, 8, 0.72f },   // double barrel: two shells, a 20 degree cone, everyone in it
        { TEXT("RIFLE"),      TEXT("SM_Rifle"),        100.0f, 8, 0.35f, 50.0f, 2, 1.0f, 1, 1.25f },   // drops a heavy in one, goes through to the man behind
        { TEXT("LONG COLT"),  TEXT("SM_Revolver_Long"), 44.0f, 7, 0.20f, 25.0f, 1, 1.15f, 1, 1.10f },  // quick, seven rounds
    };

    constexpr float AimAssistDegrees = 7.0f;        // how far off a shot may be and still hit
    constexpr float RoofHalfWidthCm = 115.0f;       // how far from the centreline anyone may lean

    FFGNetInput Pack(const FFGTrackerState& S, bool bTrackerLive)
    {
        FFGNetInput N;
        N.AimX = uint16(FMath::Clamp(S.AimX, 0.0f, 1.0f) * 65535.0f);
        N.AimY = uint16(FMath::Clamp(S.AimY, 0.0f, 1.0f) * 65535.0f);
        N.Lean = int8(FMath::Clamp(S.Lean, -1.0f, 1.0f) * 127.0f);
        N.Duck = uint8(FMath::Clamp(S.Duck, 0.0f, 1.0f) * 255.0f);
        N.Flags = (S.bAimValid ? 1 : 0) | (S.bGunPose ? 2 : 0) | (S.bHolstered ? 4 : 0) | (S.bTracking ? 8 : 0) | (bTrackerLive ? 16 : 0);
        return N;
    }

    FFGTrackerState Unpack(const FFGNetInput& N)
    {
        FFGTrackerState S;
        S.AimX = N.AimX / 65535.0f;
        S.AimY = N.AimY / 65535.0f;
        S.Lean = N.Lean / 127.0f;
        S.Duck = N.Duck / 255.0f;
        S.bAimValid = (N.Flags & 1) != 0;
        S.bGunPose = (N.Flags & 2) != 0;
        S.bHolstered = (N.Flags & 4) != 0;
        S.bTracking = (N.Flags & 8) != 0;
        S.Aim2X = S.AimX;
        S.Aim2Y = S.AimY;
        return S;
    }
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

    // Co-op: the body is placed from the seat and the replicated lean every frame. There are no moves for the
    // character movement component to send or correct, so it does not run at all.
    SetReplicateMovement(false);
    bAlwaysRelevant = true;
    SetNetUpdateFrequency(30.0f);
    GetCharacterMovement()->PrimaryComponentTick.bCanEverTick = false;

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

    // The guns in view are for the player holding them. The partner sees the cowboy (the character mesh) instead.
    Revolver = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Revolver"));
    Revolver->SetupAttachment(FirstPersonCamera);
    Revolver->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Revolver->SetCastShadow(false);
    Revolver->bOnlyOwnerSee = true;

    LongGun = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LongGun"));
    LongGun->SetupAttachment(Revolver);
    LongGun->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    LongGun->SetCastShadow(false);
    LongGun->SetVisibility(false);
    LongGun->bOnlyOwnerSee = true;

    RevolverL = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("RevolverL"));
    RevolverL->SetupAttachment(FirstPersonCamera);
    RevolverL->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RevolverL->SetCastShadow(false);
    RevolverL->SetRelativeScale3D(FVector(-1.0f, 1.0f, 1.0f));      // mirrored: the materials are two-sided
    RevolverL->bOnlyOwnerSee = true;

    GetMesh()->SetOwnerNoSee(true);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->bCastHiddenShadow = false;
    GetMesh()->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -96.0f), FRotator(0.0, -90.0, 0.0));     // modelled facing +Y

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

void AFGTrainPlayer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AFGTrainPlayer, WeaponIndex);
    DOREPLIFETIME(AFGTrainPlayer, Hats);
    DOREPLIFETIME(AFGTrainPlayer, bDowned);
    DOREPLIFETIME(AFGTrainPlayer, bGunHidden);
    DOREPLIFETIME(AFGTrainPlayer, SeatLocation);
    DOREPLIFETIME(AFGTrainPlayer, SeatYaw);
    DOREPLIFETIME_CONDITION(AFGTrainPlayer, NetInput, COND_SkipOwner);
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
    bAutoPlay = FParse::Param(FCommandLine::Get(), TEXT("FGAuto"));
    Presentation = AFGPresentation::Get(this);
    ApplySeat();
}

void AFGTrainPlayer::ApplySeat()
{
    if (SeatLocation.IsZero()) { return; }
    // On a client this copy arrives wherever the other player happened to be leaning. The seat is the real place.
    SetActorRotation(FRotator(0.0f, SeatYaw, 0.0f));
    StartingRightVector = GetActorRightVector();
    StartingActorLocation = SeatLocation;
    UpdateBodyTransform();
    // On the host every cowboy keeps his bones up to date, seen or not: a 1v1 shot is judged against them.
    GetMesh()->VisibilityBasedAnimTickOption = GetNetMode() == NM_ListenServer ? EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones : EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    if (GetMesh()->GetSkeletalMeshAsset() == nullptr || GetMesh()->GetSkeletalMeshAsset() != FGAssets::SkeletalMesh(TEXT("characters"), SeatLocation.Y > 0.0f || SeatLocation.X < 0.0f ? TEXT("SK_Gunslinger") : TEXT("SK_Deputy")))
    {
        GetMesh()->SetSkeletalMesh(FGAssets::SkeletalMesh(TEXT("characters"), SeatLocation.Y > 0.0f || SeatLocation.X < 0.0f ? TEXT("SK_Gunslinger") : TEXT("SK_Deputy")));
        AvatarPose = NAME_None;
    }
}

void AFGTrainPlayer::OnRep_Seat() { ApplySeat(); }
void AFGTrainPlayer::OnRep_WeaponIndex() { SetWeapon(WeaponIndex); }

void AFGTrainPlayer::OnRep_Hats()
{
    // The partner's cowboy loses his hat with the first hit too.
    if (Hats < 3) { GetMesh()->HideBoneByName(TEXT("hat"), PBO_None); }
    else { GetMesh()->UnHideBoneByName(TEXT("hat")); }
}

AFGGameState* AFGTrainPlayer::GS() const
{
    return GetWorld() ? GetWorld()->GetGameState<AFGGameState>() : nullptr;
}

AFGPlayerState* AFGTrainPlayer::FGPlayerState() const
{
    return GetPlayerState<AFGPlayerState>();
}

FFGTrackerState AFGTrainPlayer::InputState() const
{
    return IsLocallyControlled() ? Tracker->State : Unpack(NetInput);
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
    AFGGameState* State = GS();
    const bool bLocal = IsLocallyControlled();
    const FFGTrackerState In = InputState();
    // Two on one roof: nobody leans out past the edge (or through a tunnel wall) from a seat off the centreline.
    const float SeatY = float(SeatLocation.Y);
    const float MinLean = FMath::Clamp((-RoofHalfWidthCm - SeatY) / LeanDistance, -1.0f, 0.0f);
    const float MaxLean = FMath::Clamp((RoofHalfWidthCm - SeatY) / LeanDistance, 0.0f, 1.0f);
    SetBodyInput(FMath::Clamp(In.Lean, MinLean, MaxLean), 1.0f - In.Duck);
    if (bLocal) { SetAimNormalized(In.AimX * 2.0f - 1.0f, 1.0f - In.AimY * 2.0f); }
    Super::Tick(DeltaTime);

    // Under a tunnel roof (5.9 m, the standing eye is at 6.0) the view is held below it so it cannot poke through.
    // Down and out, it drops to the roof.
    const AFGPresentation* Pres = Presentation.Get();
    const float Speed = Pres ? Pres->Speed() : 0.0f;
    const bool bUnderRoof = Pres && Pres->World && Pres->World->MetresTo(TEXT("dark")) == 0.0f && Speed > 3.0f;
    ForcedDropCm = bDowned ? 70.0f : (bUnderRoof ? 45.0f : 0.0f);
    ForcedDropNow = FMath::FInterpTo(ForcedDropNow, ForcedDropCm, DeltaTime, 9.0f);
    CameraArm->SocketOffset = FVector(0.0f, 0.0f, -FMath::Max(DuckDropCm * In.Duck, ForcedDropNow));

    HitFlash = FMath::Max(0.0f, HitFlash - DeltaTime * 1.2f);
    HitMarker = FMath::Max(0.0f, HitMarker - DeltaTime * 4.0f);
    Recoil = FMath::FInterpTo(Recoil, 0.0f, DeltaTime, 9.0f);
    Rumble = FMath::Min(Speed / FG::CruiseSpeed, 1.4f);
    if (HasAuthority()) { InvulnerableFor = FMath::Max(0.0f, InvulnerableFor - DeltaTime); }

    // Train rumble and sway: a little roll and bob that grows with speed.
    const float T = GetWorld()->GetTimeSeconds();
    const FRotator Sway(
        FMath::Sin(T * 7.3f) * 0.18f * Rumble + Recoil * 1.6f,
        FMath::Sin(T * 0.9f) * 0.35f * Rumble,
        FMath::Sin(T * 1.7f) * 0.7f * Rumble + In.Lean * 2.5f);
    FirstPersonCamera->SetRelativeRotation(CameraBaseRotation + Sway);

    if (!bLocal || GetNetMode() == NM_ListenServer)
    {
        TickAvatar(In, DeltaTime);      // the host poses its own cowboy too: the partner sees it, and shoots at it in a 1v1
    }
    if (!bLocal)
    {
        return;
    }
    RecoilL = FMath::FInterpTo(RecoilL, 0.0f, DeltaTime, 9.0f);
    const bool bDown = In.bHolstered || !In.bAimValid || bGunHidden;
    HolsterBlend = FMath::FInterpTo(HolsterBlend, bDown ? 1.0f : 0.0f, DeltaTime, 10.0f);
    DualBlend = FMath::FInterpTo(DualBlend, In.bAim2Valid && !bGunHidden ? 1.0f : 0.0f, DeltaTime, 8.0f);
    const FVector2D Aim1(In.AimX, In.AimY), Aim2(In.Aim2X, In.Aim2Y);
    const bool bSwap = IsDual() && !In.bPrimaryOnRight;
    PoseGun(Revolver, false, bSwap ? Aim2 : Aim1, bSwap ? 1.0f - DualBlend : HolsterBlend, bSwap ? RecoilL : Recoil);
    PoseGun(RevolverL, true, bSwap ? Aim1 : Aim2, bSwap ? HolsterBlend : 1.0f - DualBlend, bSwap ? Recoil : RecoilL);
    RevolverL->SetVisibility(DualBlend > 0.02f);

    if (State)
    {
        // The duel's draw time is measured here, from when DRAW! reached this screen, so the network is not in it.
        if (State->ShowdownStep == 4 && LastShowdownStep != 4) { SawDrawAt = FPlatformTime::Seconds(); }
        LastShowdownStep = State->ShowdownStep;
        TickMagnet(State);
        TickAutoPlay(State, DeltaTime);
    }
    SendInput(DeltaTime);
}

void AFGTrainPlayer::SendInput(float DeltaTime)
{
    const FFGNetInput Now = Pack(Tracker->State, Tracker->bTrackerLive);
    if (HasAuthority())
    {
        NetInput = Now;         // the host's own player: straight into what the partner receives
    }
    else
    {
        // About 30 times a second, and only when something changed (plus a slow heartbeat).
        InputSendTimer -= DeltaTime;
        if (InputSendTimer <= 0.0f && (!(Now == NetInput) || InputSendTimer < -0.5f))
        {
            InputSendTimer = 1.0f / 30.0f;
            NetInput = Now;
            ServerInput(Now);
        }
    }
    if (const APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        int32 W = 0, H = 0;
        PC->GetViewportSize(W, H);
        const float Aspect = H > 0 ? float(W) / float(H) : 0.0f;
        if (Aspect > 0.0f && !FMath::IsNearlyEqual(Aspect, SentAspect, 0.01f))
        {
            SentAspect = Aspect;
            ServerAspect(Aspect);
        }
    }
}

void AFGTrainPlayer::ServerInput_Implementation(FFGNetInput In)
{
    NetInput = In;
}

void AFGTrainPlayer::ServerAspect_Implementation(float Aspect)
{
    ViewAspect = FMath::Clamp(Aspect, 0.5f, 4.0f);
}

void AFGTrainPlayer::TickAvatar(const FFGTrackerState& In, float DeltaTime)
{
    // The partner as the others see them: a cowboy on the roof who ducks, holsters and aims when they do.
    if (!GetMesh()->GetSkeletalMeshAsset()) { return; }
    const double Now = FPlatformTime::Seconds();
    // Riding together they kneel on the roof; in a 1v1 they stand and face each other.
    const AFGGameState* State = GS();
    const bool bVersus = State && State->bVersus;
    FName Want = bVersus ? TEXT("showdown_idle") : TEXT("idle");
    if (bDowned) { Want = TEXT("death_back"); }
    else if (In.Duck > 0.5f) { Want = TEXT("cover_idle"); }
    else if (In.bAimValid && !In.bHolstered && !bGunHidden) { Want = bVersus ? TEXT("aim") : TEXT("kneel_aim"); }
    if (Want != AvatarPose && Now >= AvatarBusyUntil)
    {
        AvatarPose = Want;
        if (UAnimSequence* Seq = FGAssets::Anim(TEXT("characters"), TEXT("SK_Bandit"), Want.ToString()))
        {
            GetMesh()->PlayAnimation(Seq, Want != TEXT("death_back"));
        }
    }
    // Turned a little toward where they are aiming.
    const float Yaw = bDowned ? 0.0f : (In.AimX - 0.5f) * 70.0f;
    const FRotator Current = GetMesh()->GetRelativeRotation();
    GetMesh()->SetRelativeRotation(FRotator(0.0f, FMath::FInterpTo(float(Current.Yaw), -90.0f + Yaw, DeltaTime, 6.0f), 0.0f));
}

void AFGTrainPlayer::RemoteShot()
{
    if (IsLocallyControlled() || bDowned) { return; }
    const AFGGameState* State = GS();
    if (UAnimSequence* Seq = FGAssets::Anim(TEXT("characters"), TEXT("SK_Bandit"), State && State->bVersus ? TEXT("shoot") : TEXT("kneel_shoot")))
    {
        GetMesh()->PlayAnimation(Seq, false);
        AvatarBusyUntil = FPlatformTime::Seconds() + FMath::Min(Seq->GetPlayLength(), 0.45f);
        AvatarPose = NAME_None;
    }
}

void AFGTrainPlayer::TickMagnet(AFGGameState* State)
{
    // Aim magnetism: near a target the crosshair leans onto it, harder the closer it gets. It hides the last of the
    // hand jitter exactly where it matters and makes a webcam feel like it is aiming for you, the way console shooters do.
    APlayerController* PC = Cast<APlayerController>(GetController());
    const FFGTrackerState& In = Tracker->State;
    TArray<FVector> Points;
    if (Tracker->bTrackerLive && State->Phase != EFGPhase::Result && State->Phase != EFGPhase::Title)
    {
        const bool bBossLocked = State->Phase == EFGPhase::Showdown && State->ShowdownStep != 4;
        for (TActorIterator<AFGBandit> It(GetWorld()); It; ++It)
        {
            if (!It->IsSpawned() || It->IsDown() || (It->Spec.Kind == EFGBanditKind::Boss && bBossLocked)) { continue; }
            FVector Chest, Head;
            It->AimPoints(Chest, Head);
            Points.Add(Chest);
        }
        for (TActorIterator<AFGTarget> It(GetWorld()); It; ++It)
        {
            if (It->bActive) { Points.Add(It->Centre()); }
        }
    }
    auto Magnet = [PC, &Points](FVector2D Raw, FVector2D& Offset, float RealDelta)
    {
        FVector2D Want = FVector2D::ZeroVector;
        int32 W = 0, H = 0;
        if (PC) { PC->GetViewportSize(W, H); }
        if (PC && W > 0 && H > 0)
        {
            constexpr float Reach = 0.13f;          // screen heights
            const float Aspect = float(W) / float(H);
            float BestD = Reach;
            for (const FVector& P : Points)
            {
                FVector2D Px;
                if (!PC->ProjectWorldLocationToScreen(P, Px)) { continue; }
                const FVector2D N(Px.X / W, Px.Y / H);
                const float D = FMath::Sqrt(FMath::Square((N.X - Raw.X) * Aspect) + FMath::Square(N.Y - Raw.Y));
                if (D < BestD)
                {
                    BestD = D;
                    Want = (N - Raw) * (0.75f * FMath::Pow(1.0f - D / Reach, 0.6f));
                }
            }
        }
        Offset = FMath::Vector2DInterpTo(Offset, Want, RealDelta, 12.0f);
        return Raw + Offset;
    };
    const float RealDelta = FApp::GetDeltaTime();
    AssistedAim = Magnet(FVector2D(In.AimX, In.AimY), MagnetOffset, RealDelta);
    AssistedAim2 = Magnet(FVector2D(In.Aim2X, In.Aim2Y), MagnetOffset2, RealDelta);
}

void AFGTrainPlayer::TickAutoPlay(AFGGameState* State, float DeltaTime)
{
    // -FGAuto: plays by itself, on whichever machine it is given to.
    if (!bAutoPlay) { return; }
    if (State->Phase == EFGPhase::Versus && State->ShowdownStep != 4)
    {
        AutoTimer = FMath::FRandRange(0.25f, 0.6f);      // a 1v1: its reaction starts counting at DRAW!, like a person's
        return;
    }
    AutoTimer -= DeltaTime;
    if (AutoTimer > 0.0f) { return; }
    AutoTimer = 0.7f;
    if (State->Phase == EFGPhase::Title) { return; }
    if (IsEmpty()) { Tracker->OnReload.Broadcast(); return; }
    if (State->Phase == EFGPhase::Result) { if (State->ResultTime() > 6.0f) { Tracker->OnFire.Broadcast(FVector2D(0.5, 0.82), 0); } return; }
    if (State->Phase == EFGPhase::Versus)
    {
        // A 1v1: after DRAW!, a shot at the other cowboy.
        for (TActorIterator<AFGTrainPlayer> It(GetWorld()); It; ++It)
        {
            if (*It == this) { continue; }
            APlayerController* PC = Cast<APlayerController>(GetController());
            FVector2D Screen;
            int32 VW = 0, VH = 0;
            if (PC) { PC->GetViewportSize(VW, VH); }
            const FVector At = It->GetMesh()->DoesSocketExist(TEXT("spine_02")) ? It->GetMesh()->GetSocketLocation(TEXT("spine_02")) : It->GetActorLocation();
            if (PC && VW > 0 && PC->ProjectWorldLocationToScreen(At, Screen))
            {
                Tracker->State.AimX = Screen.X / VW;
                Tracker->State.AimY = Screen.Y / VH;
                Tracker->OnFire.Broadcast(FVector2D(Screen.X / VW, Screen.Y / VH), 0);
            }
        }
        return;
    }
    if (State->Phase == EFGPhase::Showdown && State->ShowdownStep != 4) { return; }
    FVector Aim = FVector::ZeroVector;
    for (TActorIterator<AFGTarget> It(GetWorld()); It; ++It) { if (It->bActive) { Aim = It->Centre(); break; } }
    if (Aim.IsZero())
    {
        for (TActorIterator<AFGBandit> It(GetWorld()); It; ++It)
        {
            // Every other shot at the head: headshots win hats back, and bring a partner who is down back up.
            if (It->IsSpawned() && !It->IsDown() && It->State != EFGBanditState::Entering) { FVector Chest, Head; It->AimPoints(Chest, Head); Aim = ++AutoShots % 2 ? Chest : Head; break; }
        }
    }
    APlayerController* PC = Cast<APlayerController>(GetController());
    FVector2D Screen;
    int32 VW = 0, VH = 0;
    if (PC) { PC->GetViewportSize(VW, VH); }
    if (PC && !Aim.IsZero() && PC->ProjectWorldLocationToScreen(Aim, Screen) && VW > 0)
    {
        Tracker->State.AimX = Screen.X / VW;
        Tracker->State.AimY = Screen.Y / VH;
        Tracker->OnFire.Broadcast(FVector2D(Screen.X / VW, Screen.Y / VH), 0);
    }
}

void AFGTrainPlayer::HandleFire(FVector2D Aim, int32 Gun)
{
    AFGGameState* State = GS();
    AFGPlayerController* PC = Cast<AFGPlayerController>(GetController());
    if (!State || !PC)
    {
        return;
    }
    if (State->Phase == EFGPhase::Title)
    {
        if (PC->IsMenuOpen()) { PC->MenuShot(Aim); }       // the main menu is shot at, like everything else
        return;
    }
    if (State->Phase == EFGPhase::Result)
    {
        // Shots only press buttons here. Any shot, not only one on the button: a player who is down has no steady
        // crosshair, and the booth must never need a keyboard.
        if (State->ResultTime() > 1.5f)
        {
            FGCombat::Sfx(GetWorld(), TEXT("shot_player"));
            PC->ServerVoteRideAgain();
        }
        return;
    }
    if (bDowned)
    {
        return;
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
            PlayGun(TEXT("fp_dry_fire"), false, 0);
            FGCombat::Sfx(GetWorld(), TEXT("dry"));
            return;
        }
        if (!CanFire() || !GetCrosshairWorldRay(Origin, Dir))
        {
            return;
        }
        // Lohith's ammo and cooldown. (His world line trace is left out: the host decides what a shot hits.)
        LastFireTime = GetWorld()->GetTimeSeconds();
        if (!bInfiniteAmmo) { CurrentAmmo = FMath::Max(0, CurrentAmmo - 1); }
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
            FGCombat::Sfx(GetWorld(), TEXT("dry"));
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
    Dir = Dir.GetSafeNormal();
    PlayGun(TEXT("fp_fire"), false, Gun);
    FVector Muzzle = Model->DoesSocketExist(TEXT("muzzle")) ? Model->GetSocketLocation(TEXT("muzzle")) : HeadLocation() + Dir * 60.0f;
    if (Gun == 0 && Weapon().Prop)
    {
        Muzzle = LongGun->GetComponentTransform().TransformPosition(FVector(0.0f, Weapon().PropLengthCm, 4.0f));
    }

    // Seen and heard at once, aimed where this screen thinks it lands. The host decides what it really hit.
    const bool bBossLocked = State->Phase == EFGPhase::Showdown && State->ShowdownStep != 4;
    const float Assist = (State->Phase == EFGPhase::Bell ? 6.0f : AimAssistDegrees) * Weapon().AssistScale;
    const FGCombat::FShotHit Preview = FGCombat::FindHit(GetWorld(), Origin, Dir, Assist, bBossLocked, State->Phase == EFGPhase::Calibrate);
    FGCombat::PlayerShot(GetWorld(), Muzzle, Preview.Point, Weapon().Tracers);
    FGCombat::Sfx(GetWorld(), TEXT("shot_player"), 1.0f, Weapon().SfxPitch);
    const int16 ReactionMs = (State->Phase == EFGPhase::Showdown || State->Phase == EFGPhase::Versus) && State->ShowdownStep == 4 && SawDrawAt > 0.0
        ? int16(FMath::Clamp((FPlatformTime::Seconds() - SawDrawAt) * 1000.0, 0.0, 30000.0)) : int16(-1);
    ServerFire(Origin, Dir, Muzzle, ReactionMs);
}

void AFGTrainPlayer::ServerFire_Implementation(FVector_NetQuantize10 Origin, FVector_NetQuantizeNormal Dir, FVector_NetQuantize10 Muzzle, int16 ReactionMs)
{
    AFGIronHorseGameMode* GM = GetWorld()->GetAuthGameMode<AFGIronHorseGameMode>();
    if (!GM) { return; }
    // Co-op trusts the shooter's aim (it is their webcam), but not a machine gun.
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now - LastShotAt < Weapon().Cooldown * 0.5) { return; }
    LastShotAt = Now;
    if (!GM->PlayerMayFire(this)) { return; }
    LastReactionMs = ReactionMs;
    AFGPlayerState* PS = FGPlayerState();
    if (PS) { ++PS->ShotsFired; }
    const bool bHit = GM->ResolvePlayerShot(this, Origin, FVector(Dir).GetSafeNormal(), Muzzle);
    if (bHit && PS) { ++PS->ShotsHit; }
    ClientShotResult(bHit);
}

void AFGTrainPlayer::ClientShotResult_Implementation(bool bHit)
{
    if (bHit) { HitMarker = 1.0f; }
}

void AFGTrainPlayer::ClientHurt_Implementation(FName Sfx)
{
    HitFlash = 1.0f;
    FGCombat::Sfx(GetWorld(), Sfx);
    Tracker->SendHit();     // to this player's own tracker, and on to their glove
}

void AFGTrainPlayer::HandleReload()
{
    AFGGameState* State = GS();
    if (State && State->Phase == EFGPhase::Result)
    {
        if (State->ResultTime() > 1.5f)
        {
            if (AFGPlayerController* PC = Cast<AFGPlayerController>(GetController())) { PC->ServerVoteRideAgain(); }
        }
        return;
    }
    if (!State || (CurrentAmmo >= MagazineSize && (!IsDual() || AmmoL >= MagazineSize)))
    {
        return;
    }
    Reload();
    AmmoL = MagazineSize;
    PlayGun(TEXT("fp_reload"), false, 0);
    if (IsDual()) { PlayGun(TEXT("fp_reload"), false, 1); }
    FGCombat::Sfx(GetWorld(), TEXT("reload"));
}

bool AFGTrainPlayer::TakeHit()
{
    SetHats(Hats - 1);
    return Hats <= 0;
}

void AFGTrainPlayer::SetHats(int32 NewHats)
{
    Hats = FMath::Clamp(NewHats, 0, 3);
    OnRep_Hats();
}

void AFGTrainPlayer::SetSeat(const FVector& Seat, float Yaw)
{
    SeatLocation = Seat;
    SeatYaw = Yaw;
    ApplySeat();
}

void AFGTrainPlayer::ClientNewGun_Implementation(int32 Index)
{
    SetWeapon(Index);
    AmmoL = MagazineSize;
}
