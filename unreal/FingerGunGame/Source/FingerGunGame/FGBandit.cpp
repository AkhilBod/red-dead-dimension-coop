#include "FGBandit.h"

#include "Animation/AnimSequence.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "FGAssets.h"
#include "FGFx.h"
#include "FGGameState.h"
#include "FGIronHorseGameMode.h"
#include "FGPresentation.h"
#include "FGTrain.h"
#include "FGTrainPlayer.h"
#include "Net/UnrealNetwork.h"

namespace
{
    constexpr float TelegraphSeconds = 0.7f;
    const TCHAR* HorseCoats[] = { TEXT("SK_Horse_Bay"), TEXT("SK_Horse_Black"), TEXT("SK_Horse_Grey"), TEXT("SK_Horse_Palomino") };
}

AFGBandit::AFGBandit()
{
    PrimaryActorTick.bCanEverTick = true;
    bDestroyOnDeath = false;
    bReplicates = true;
    SetReplicateMovement(false);        // every machine places it from its anchor: see Place()
    bAlwaysRelevant = true;
    SetNetUpdateFrequency(30.0f);
    EnemyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
    Body->SetupAttachment(Root);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // Modelled facing -Y in Blender, +Y after import. Actor forward is +X.
    Body->SetRelativeRotation(FRotator(0.0, -90.0, 0.0));
}

void AFGBandit::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(AFGBandit, Spec, COND_InitialOnly);
    DOREPLIFETIME(AFGBandit, State);
    DOREPLIFETIME(AFGBandit, Target);
    DOREPLIFETIME(AFGBandit, Motion);
    DOREPLIFETIME(AFGBandit, Anim);
    DOREPLIFETIME(AFGBandit, bNetDead);
    DOREPLIFETIME(AFGBandit, WarnFrom);
    DOREPLIFETIME(AFGBandit, WarnUntil);
}

void AFGBandit::Init(const FFGBanditSpec& InSpec, AFGIronHorseGameMode* InGame)
{
    Spec = InSpec;
    Game = InGame;
    Spec.SpawnedAt = Now();
    MaxHealth = CurrentHealth = Spec.Health;
    Timer = Spec.FirstShotDelay;
    Build();
    switch (Spec.Kind)
    {
    case EFGBanditKind::Rider:
        Play(TEXT("ride_gallop"), true);
        Local = FVector(-5500.0f, Spec.Slot.Y * 1.25f, 0.0f);
        break;
    case EFGBanditKind::Boarder:
        Play(TEXT("climb"), true);
        Local = Spec.Slot + FVector(0.0f, Side * 110.0f, -230.0f);
        break;
    case EFGBanditKind::Boss:
        Play(TEXT("idle"), true);
        Local = Spec.Slot + FVector(0.0f, 0.0f, 900.0f);
        break;
    default:
        // Crews of the other train haul themselves up its far side. They used to pop into existence on the roof.
        Play(TEXT("climb"), true);
        Local = Spec.Slot + FVector(0.0f, -120.0f, -230.0f);
        break;
    }
    Motion.Local = Local;
    Motion.At = Now();
    SetState(EFGBanditState::Entering);
    Place(0.0f);
}

void AFGBandit::BeginPlay()
{
    Super::BeginPlay();
    Presentation = AFGPresentation::Get(this);
    if (Presentation.IsValid()) { AddTickPrerequisiteActor(Presentation.Get()); }
    if (!HasAuthority() && !bBuilt && Spec.SpawnedAt > 0.0) { Build(); }
}

void AFGBandit::Build()
{
    if (bBuilt) { return; }
    bBuilt = true;
    if (!Presentation.IsValid()) { Presentation = AFGPresentation::Get(this); }
    AnimMesh = (Spec.Mesh == TEXT("SK_Heavy") || Spec.Mesh == TEXT("SK_Boss")) ? Spec.Mesh : TEXT("SK_Bandit");
    Body->SetSkeletalMesh(FGAssets::SkeletalMesh(TEXT("characters"), Spec.Mesh));
    // The host judges shots against the bones, also for a partner's view of a bandit the host is not looking at.
    Body->VisibilityBasedAnimTickOption = GetNetMode() == NM_ListenServer ? EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones : EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    Side = Spec.Slot.Y >= 0.0f ? 1.0f : -1.0f;
    SetActorScale3D(FVector(Spec.Scale));

    WarnLight = NewObject<UPointLightComponent>(this);
    WarnLight->SetMobility(EComponentMobility::Movable);
    WarnLight->SetupAttachment(Body, Body->DoesSocketExist(TEXT("muzzle_r")) ? FName(TEXT("muzzle_r")) : NAME_None);
    WarnLight->SetIntensityUnits(ELightUnits::Candelas);
    WarnLight->SetIntensity(0.0f);
    WarnLight->SetLightColor(FLinearColor(1.0f, 0.04f, 0.02f));
    WarnLight->SetAttenuationRadius(900.0f);
    WarnLight->SetCastShadows(false);
    WarnLight->SetVisibility(false);
    WarnLight->RegisterComponent();

    if (Spec.Kind == EFGBanditKind::Rider)
    {
        Horse = NewObject<USkeletalMeshComponent>(this);
        Horse->SetSkeletalMesh(FGAssets::SkeletalMesh(TEXT("horses"), HorseCoats[Spec.HorseCoat % 4]));
        Horse->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Horse->SetupAttachment(Root);
        Horse->SetRelativeRotation(FRotator(0.0, -90.0, 0.0));
        Horse->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
        Horse->RegisterComponent();
        if (UAnimSequence* Gallop = FGAssets::Anim(TEXT("horses"), TEXT("SK_Horse_Bay"), TEXT("horse_gallop")))
        {
            Horse->PlayAnimation(Gallop, true);
        }
    }
    else if (Spec.Kind == EFGBanditKind::Boarder)
    {
        Cover = NewObject<UStaticMeshComponent>(this);
        Cover->SetStaticMesh(FGAssets::StaticMesh(TEXT("props"), TEXT("SM_Crate")));
        Cover->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Cover->SetupAttachment(Root);
        Cover->SetUsingAbsoluteLocation(true);
        Cover->SetUsingAbsoluteRotation(true);
        Cover->RegisterComponent();
    }

    if (!HasAuthority())
    {
        // A client catching up: wherever and whatever the host has it doing now.
        Local = Motion.Local;
        if (!Anim.Action.IsNone()) { PlayLocal(Anim.Action, Anim.bLoop, Anim.Rate); }
        if (bNetDead) { DieCosmetic(); }
    }
}

void AFGBandit::OnRep_Spec()
{
    Build();
    Place(0.0f);
}

void AFGBandit::OnRep_Anim()
{
    if (bBuilt) { PlayLocal(Anim.Action, Anim.bLoop, Anim.Rate); }
}

void AFGBandit::OnRep_Dead()
{
    if (bBuilt && bNetDead) { DieCosmetic(); }
}

double AFGBandit::Now() const
{
    const AFGGameState* GS = GetWorld()->GetGameState<AFGGameState>();
    return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

float AFGBandit::Play(const FString& Action, bool bLoop, float Rate)
{
    // Host: here, and to everyone else through Anim.
    Anim.Action = FName(*Action);
    Anim.bLoop = bLoop;
    Anim.Rate = Rate;
    ++Anim.Seq;
    UAnimSequence* Seq = FGAssets::Anim(TEXT("characters"), AnimMesh, Action);
    if (!Seq)
    {
        return 0.5f;
    }
    Body->PlayAnimation(Seq, bLoop);
    Body->SetPlayRate(Rate);
    return Seq->GetPlayLength() / FMath::Max(0.01f, Rate);
}

void AFGBandit::PlayLocal(FName Action, bool bLoop, float Rate)
{
    if (UAnimSequence* Seq = FGAssets::Anim(TEXT("characters"), AnimMesh, Action.ToString()))
    {
        Body->PlayAnimation(Seq, bLoop);
        Body->SetPlayRate(Rate);
    }
}

FTransform AFGBandit::AnchorTransform() const
{
    return Presentation.IsValid() ? Presentation->AnchorTransform(Spec.Anchor) : FTransform::Identity;
}

void AFGBandit::SetState(EFGBanditState NewState)
{
    State = NewState;
    StateTime = 0.0f;
}

void AFGBandit::FacePlayers()
{
    // Whoever it is shooting at, or the nearest player still standing.
    const AFGTrainPlayer* Face = IsValid(Target) && !Target->bDowned ? Target.Get() : nullptr;
    if (!Face)
    {
        float Best = TNumericLimits<float>::Max();
        for (TActorIterator<AFGTrainPlayer> It(GetWorld()); It; ++It)
        {
            const float D = FVector::DistSquared(It->GetActorLocation(), GetActorLocation());
            if (!It->bDowned && D < Best) { Best = D; Face = *It; }
        }
    }
    if (!Face) { return; }
    const FVector To = Face->HeadLocation() - GetActorLocation();
    SetActorRotation(FRotator(0.0, To.Rotation().Yaw, 0.0));
}

FVector AFGBandit::MuzzleLocation() const
{
    if (Body->DoesSocketExist(TEXT("muzzle_r")))
    {
        return Body->GetSocketLocation(TEXT("muzzle_r"));
    }
    FVector Chest, Head;
    AimPoints(Chest, Head);
    return Chest;
}

void AFGBandit::AimPoints(FVector& OutChest, FVector& OutHead) const
{
    const float Scale = Body->GetComponentScale().Z;
    const bool bBones = Body->DoesSocketExist(TEXT("head")) && Body->DoesSocketExist(TEXT("spine_02"));
    OutHead = bBones ? Body->GetSocketLocation(TEXT("head")) + FVector(0, 0, 14) : Body->GetComponentLocation() + FVector(0, 0, 168.0f * Scale);
    OutChest = bBones ? Body->GetSocketLocation(TEXT("spine_02")) : Body->GetComponentLocation() + FVector(0, 0, 115.0f * Scale);
}

float AFGBandit::Warning() const
{
    if (IsDown() || WarnUntil <= 0.0) { return 0.0f; }
    const double T = Now();
    if (T >= WarnUntil) { return 0.0f; }
    return FMath::Clamp(float((T - WarnFrom) / FMath::Max(WarnUntil - WarnFrom, 0.01)), 0.02f, 1.0f);
}

void AFGBandit::Leave()
{
    if (State != EFGBanditState::Dead)
    {
        ReleaseToken();
        WarnUntil = 0.0;
        SetState(EFGBanditState::Leaving);
    }
}

void AFGBandit::ReleaseToken()
{
    if (TokenFrom.IsValid() && Game.IsValid()) { Game->ReleaseAttackToken(TokenFrom.Get()); }
    TokenFrom = nullptr;
}

void AFGBandit::Draw(float ReactionSeconds)
{
    DrawTimer = ReactionSeconds;
    WarnFrom = Now() + ReactionSeconds - 1.0;
    WarnUntil = Now() + ReactionSeconds;
    Play(TEXT("quickdraw"));
}

void AFGBandit::FireAtPlayer(bool bFast)
{
    if (!Game.IsValid()) { return; }
    if (Spec.Kind == EFGBanditKind::Dynamiter)
    {
        Game->SpawnDynamite(MuzzleLocation(), Target);
        return;
    }
    if (Spec.Kind == EFGBanditKind::Boss)
    {
        // The duel: one bullet for everyone still standing, a beat apart.
        int32 i = 0;
        for (AFGTrainPlayer* P : Game->AlivePlayers()) { Game->SpawnEnemyShot(MuzzleLocation(), bFast, P, 0.15f * i++); }
        return;
    }
    Game->SpawnEnemyShot(MuzzleLocation(), bFast, Target, 0.0f);
}

void AFGBandit::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!bBuilt) { return; }

    // Red flash at the barrel, quicker as the shot gets nearer.
    const float Warn = Warning();
    const bool bBlinkOn = Warn > 0.0f && FMath::Fmod(float(Now() - Spec.SpawnedAt) * (5.0f + 9.0f * Warn), 1.0f) < 0.55f;
    WarnLight->SetIntensity(bBlinkOn ? 2500.0f : 0.0f);
    WarnLight->SetVisibility(bBlinkOn);        // an unlit light still costs a light

    if (HasAuthority())
    {
        if (!Game.IsValid()) { return; }
        const FVector Before = Local;
        TickHost(DeltaTime);
        if (!IsValid(this) || IsActorBeingDestroyed()) { return; }
        if (!Local.Equals(Motion.Local, 0.5))
        {
            Motion.Velocity = DeltaTime > 0.0f ? FVector((Local - Before) / DeltaTime) : FVector::ZeroVector;
            Motion.Local = Local;
            Motion.At = Now();
        }
    }
    else
    {
        // Carry it on from the last update at its last speed, and ease toward that rather than jump.
        const FVector Predicted = FVector(Motion.Local) + FVector(Motion.Velocity) * FMath::Clamp(Now() - Motion.At, 0.0, 0.25);
        Local = FVector::DistSquared(Local, Predicted) > FMath::Square(300.0f) ? Predicted : FMath::VInterpTo(Local, Predicted, DeltaTime, 12.0f);
    }
    Place(DeltaTime);
}

void AFGBandit::TickHost(float DeltaTime)
{
    StateTime += DeltaTime;
    const bool bRider = Spec.Kind == EFGBanditKind::Rider;

    if (State == EFGBanditState::Dead)
    {
        // Left behind by the train.
        const float Speed = Game->TrainSpeed * 100.0f;
        if (bRider)
        {
            DeadVelocity.X = FMath::FInterpTo(DeadVelocity.X, -Speed, DeltaTime, 1.6f);
        }
        else if (StateTime > 0.35f)
        {
            DeadVelocity.Y = Side * 320.0f;
            DeadVelocity.Z -= 980.0f * DeltaTime;
            if (Local.Z < Spec.Slot.Z - 150.0f) { DeadVelocity.X = FMath::FInterpTo(DeadVelocity.X, -Speed, DeltaTime, 3.0f); }
        }
        Local += DeadVelocity * DeltaTime;
        Local.Z = FMath::Max(Local.Z, bRider ? 0.0f : -20.0f);
        // (The horse is part of this actor, so it pulls up and is left behind with him. It used to gallop on
        // riderless, straight out over the next canyon.)
        if (StateTime > 4.0f) { Destroy(); }
        return;
    }

    switch (State)
    {
    case EFGBanditState::Entering:
        if (bRider)
        {
            Local.X = FMath::FInterpTo(Local.X, Spec.Slot.X, DeltaTime, 1.1f);
            Local.Y = FMath::FInterpTo(Local.Y, Spec.Slot.Y, DeltaTime, 0.8f);
            if (FMath::Abs(Local.X - Spec.Slot.X) < 250.0f) { SetState(EFGBanditState::Idle); }
        }
        else if (Spec.Kind == EFGBanditKind::Boarder)
        {
            const float A = FMath::Clamp(StateTime / 1.5f, 0.0f, 1.0f);
            Local = FMath::Lerp(Spec.Slot + FVector(0.0f, Side * 110.0f, -230.0f), Spec.Slot, FMath::SmoothStep(0.0f, 1.0f, A));
            if (A >= 1.0f) { Play(TEXT("cover_idle"), true); SetState(EFGBanditState::Idle); }
        }
        else if (Spec.Kind == EFGBanditKind::Boss)
        {
            Local.Z = FMath::FInterpConstantTo(Local.Z, Spec.Slot.Z, DeltaTime, 1400.0f);
            if (Local.Z <= Spec.Slot.Z + 1.0f) { Play(TEXT("showdown_idle"), true); SetState(EFGBanditState::Scripted); Game->OnBossLanded(); }
        }
        else
        {
            const float A = FMath::Clamp(StateTime / 1.4f, 0.0f, 1.0f);
            Local = FMath::Lerp(Spec.Slot + FVector(0.0f, -120.0f, -230.0f), Spec.Slot, FMath::SmoothStep(0.0f, 1.0f, A));
            if (A >= 1.0f)
            {
                Play(Spec.Mesh == TEXT("SK_Rifleman") ? TEXT("rifle_idle") : TEXT("idle"), true);
                SetState(EFGBanditState::Idle);
            }
        }
        break;

    case EFGBanditState::Idle:
        Timer -= DeltaTime;
        if (Timer <= 0.0f)
        {
            // Never from behind or off screen: a shot the player could not see is not dodgeable. With two on the
            // roof it goes for whoever can see it and has the fewest guns on them already.
            FVector Chest, Head;
            AimPoints(Chest, Head);
            AFGTrainPlayer* Victim = Game->PickTarget(Chest);
            if (!Victim) { break; }
            TokenFrom = Victim;
            Target = Victim;
            bShotThisTelegraph = false;
            if (bRider) { Play(Side > 0.0f ? TEXT("ride_aim_left") : TEXT("ride_aim_right")); }
            else if (Spec.Kind == EFGBanditKind::Dynamiter) { Play(TEXT("throw")); }
            else if (Spec.Mesh == TEXT("SK_Rifleman")) { Play(TEXT("rifle_aim_start")); }
            else if (Spec.Mesh == TEXT("SK_Heavy")) { Play(TEXT("rifle_aim_start")); }
            else { Play(TEXT("aim_start")); }
            WarnFrom = Now();
            WarnUntil = WarnFrom + TelegraphSeconds;
            Game->OnTelegraph(this);
            SetState(EFGBanditState::Telegraph);
        }
        break;

    case EFGBanditState::Telegraph:
        if (StateTime >= TelegraphSeconds && !bShotThisTelegraph)
        {
            bShotThisTelegraph = true;
            WarnUntil = 0.0;
            FireAtPlayer(false);
            if (bRider) { Play(Side > 0.0f ? TEXT("ride_shoot_left") : TEXT("ride_shoot_right")); }
            else if (Spec.Kind == EFGBanditKind::Dynamiter) { }
            else if (Spec.Mesh == TEXT("SK_Rifleman")) { Play(TEXT("rifle_shoot")); }
            else if (Spec.Mesh == TEXT("SK_Heavy")) { Play(TEXT("shotgun_shoot")); }
            else { Play(TEXT("shoot")); }
        }
        if (StateTime >= TelegraphSeconds + 0.8f)
        {
            ReleaseToken();
            if (bRider) { Play(TEXT("ride_gallop"), true); }
            else if (Spec.Kind == EFGBanditKind::Boarder) { Play(TEXT("cover_idle"), true); }
            else { Play(Spec.Mesh == TEXT("SK_Rifleman") || Spec.Mesh == TEXT("SK_Heavy") ? TEXT("rifle_idle") : TEXT("idle"), true); }
            Timer = FMath::FRandRange(2.0f, 3.8f) * Game->FireDelayScale();
            SetState(EFGBanditState::Idle);
        }
        break;

    case EFGBanditState::Leaving:
        if (bRider)
        {
            // Reins in: drops back at up to the train's own speed, which is standing still on the ground.
            DeadVelocity.X = FMath::FInterpTo(DeadVelocity.X, -Game->TrainSpeed * 100.0f, DeltaTime, 1.2f);
            Local.X += DeadVelocity.X * DeltaTime;
            if (StateTime > 5.0f) { Destroy(); return; }
        }
        else if (Spec.Kind == EFGBanditKind::Boarder || Spec.Kind == EFGBanditKind::Boss || !Spec.Anchor.IsSet())
        {
            // Bails out over the side of our train.
            DeadVelocity.Y = Side * 380.0f;
            DeadVelocity.Z -= 980.0f * DeltaTime;
            if (Local.Z < Spec.Slot.Z - 150.0f) { DeadVelocity.X = FMath::FInterpTo(DeadVelocity.X, -Game->TrainSpeed * 100.0f, DeltaTime, 3.0f); }
            Local += DeadVelocity * DeltaTime;
            if (StateTime > 3.0f) { Destroy(); return; }
        }
        else if (StateTime > 20.0f)
        {
            Destroy();          // crew of the other train: they just ride it away as it drops behind
            return;
        }
        break;

    case EFGBanditState::Scripted:
        if (DrawTimer >= 0.0f)
        {
            DrawTimer -= DeltaTime;
            if (DrawTimer < 0.0f)
            {
                WarnUntil = 0.0;
                Play(TEXT("shoot"));
                FireAtPlayer(true);
            }
        }
        break;

    default:
        break;
    }
}

void AFGBandit::Place(float DeltaTime)
{
    const bool bRider = Spec.Kind == EFGBanditKind::Rider;
    if (IsDown() || State == EFGBanditState::Dead)
    {
        SetActorLocation((FTransform(Local) * AnchorTransform()).GetLocation());
        return;
    }
    FVector Shown = Local;
    if (bRider)
    {
        const float Age = float(Now() - Spec.SpawnedAt);
        Shown.X += FMath::Sin(Age * 0.7f + Spec.Slot.X) * 160.0f;
        Shown.Y += FMath::Sin(Age * 0.45f + Spec.Slot.Y) * 90.0f;
    }
    const FTransform World = FTransform(Shown) * AnchorTransform();
    SetActorLocation(World.GetLocation());
    if (bRider)
    {
        SetActorRotation(World.GetRotation());
    }
    else
    {
        FacePlayers();
    }
    if (bRider && Horse)
    {
        // art/README.md: rider root sits 90 cm under the saddle bone. Done in world space: the bone's own axes are Blender's.
        Body->SetWorldLocation(Horse->GetSocketLocation(TEXT("saddle")) - FVector(0.0f, 0.0f, 90.0f * Spec.Scale));
    }
    if (Cover)
    {
        const FTransform Anchor = AnchorTransform();
        Cover->SetWorldLocationAndRotation((FTransform(Spec.Slot + FVector(-85.0f, 0.0f, 0.0f)) * Anchor).GetLocation(), Anchor.GetRotation());
    }
}

void AFGBandit::Die()
{
    if (bIsDead) { return; }
    Super::Die();
    ReleaseToken();
    bNetDead = true;
    WarnUntil = 0.0;
    Play(Spec.Kind == EFGBanditKind::Rider ? TEXT("ride_death") : TEXT("death_back"));
    DieCosmetic();
    DeadVelocity = FVector::ZeroVector;
    SetState(EFGBanditState::Dead);
    if (Game.IsValid()) { Game->OnBanditKilled(this); }
}

void AFGBandit::DieCosmetic()
{
    if (bCosmeticDeath) { return; }
    bCosmeticDeath = true;
    if (Spec.Kind == EFGBanditKind::Rider && Horse)
    {
        Horse->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    }
    // Hat off.
    Body->HideBoneByName(TEXT("hat"), PBO_None);
    FVector Chest, Head;
    AimPoints(Chest, Head);
    const FString Hat = TEXT("SM_Hat_") + Spec.Mesh.RightChop(3);
    if (AFGFx* Fx = AFGFx::Spawn(GetWorld(), TEXT("props"), Hat, FTransform(GetActorRotation(), Head), 2.5f,
        FVector(-600.0f, Side * 150.0f, 520.0f), 0.0f, 980.0f, FRotator(200.0f, 340.0f, 0.0f)))
    {
        Fx->Mesh->SetCastShadow(true);
    }
}
