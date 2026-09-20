#include "FGBandit.h"

#include "Animation/AnimSequence.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "FGAssets.h"
#include "FGFx.h"
#include "FGIronHorseGameMode.h"
#include "FGTrain.h"
#include "FGTrainPlayer.h"

namespace
{
    constexpr float TelegraphSeconds = 0.7f;
    const TCHAR* HorseCoats[] = { TEXT("SK_Horse_Bay"), TEXT("SK_Horse_Black"), TEXT("SK_Horse_Grey"), TEXT("SK_Horse_Palomino") };
}

AFGBandit::AFGBandit()
{
    PrimaryActorTick.bCanEverTick = true;
    bDestroyOnDeath = false;
    EnemyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
    Body->SetupAttachment(Root);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    // Modelled facing -Y in Blender, +Y after import. Actor forward is +X.
    Body->SetRelativeRotation(FRotator(0.0, -90.0, 0.0));
}

void AFGBandit::Init(const FFGBanditSpec& InSpec, AFGIronHorseGameMode* InGame)
{
    Spec = InSpec;
    Game = InGame;
    MaxHealth = CurrentHealth = Spec.Health;
    AnimMesh = (Spec.Mesh == TEXT("SK_Heavy") || Spec.Mesh == TEXT("SK_Boss")) ? Spec.Mesh : TEXT("SK_Bandit");
    Body->SetSkeletalMesh(FGAssets::SkeletalMesh(TEXT("characters"), Spec.Mesh));
    Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    Side = Spec.Slot.Y >= 0.0f ? 1.0f : -1.0f;
    Local = Spec.Slot;
    Timer = Spec.FirstShotDelay;
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

    switch (Spec.Kind)
    {
    case EFGBanditKind::Rider:
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
        Play(TEXT("ride_gallop"), true);
        Local = FVector(-5500.0f, Spec.Slot.Y * 1.25f, 0.0f);
        break;
    }
    case EFGBanditKind::Boarder:
    {
        Cover = NewObject<UStaticMeshComponent>(this);
        Cover->SetStaticMesh(FGAssets::StaticMesh(TEXT("props"), TEXT("SM_Crate")));
        Cover->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Cover->SetupAttachment(Root);
        Cover->SetUsingAbsoluteLocation(true);
        Cover->SetUsingAbsoluteRotation(true);
        Cover->RegisterComponent();
        Play(TEXT("climb"), true);
        Local = Spec.Slot + FVector(0.0f, Side * 110.0f, -230.0f);
        break;
    }
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
    SetState(EFGBanditState::Entering);
}

float AFGBandit::Play(const FString& Action, bool bLoop, float Rate)
{
    UAnimSequence* Seq = FGAssets::Anim(TEXT("characters"), AnimMesh, Action);
    if (!Seq)
    {
        return 0.5f;
    }
    Body->PlayAnimation(Seq, bLoop);
    Body->SetPlayRate(Rate);
    return Seq->GetPlayLength() / FMath::Max(0.01f, Rate);
}

FTransform AFGBandit::AnchorTransform() const
{
    return Spec.Anchor ? Spec.Anchor() : FTransform::Identity;
}

void AFGBandit::SetState(EFGBanditState NewState)
{
    State = NewState;
    StateTime = 0.0f;
}

void AFGBandit::FacePlayer()
{
    if (!Game.IsValid() || !Game->Player) { return; }
    const FVector To = Game->Player->HeadLocation() - GetActorLocation();
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
    if (bIsDead) { return 0.0f; }
    if (State == EFGBanditState::Telegraph && !bShotThisTelegraph) { return FMath::Clamp(StateTime / TelegraphSeconds, 0.02f, 1.0f); }
    if (State == EFGBanditState::Scripted && DrawTimer > 0.0f) { return FMath::Clamp(1.0f - DrawTimer, 0.02f, 1.0f); }
    return 0.0f;
}

void AFGBandit::Leave()
{
    if (State != EFGBanditState::Dead)
    {
        if (bHasToken && Game.IsValid()) { Game->ReleaseAttackToken(); bHasToken = false; }
        SetState(EFGBanditState::Leaving);
    }
}

void AFGBandit::Draw(float ReactionSeconds)
{
    DrawTimer = ReactionSeconds;
    Play(TEXT("quickdraw"));
}

void AFGBandit::FireAtPlayer(bool bFast)
{
    if (!Game.IsValid()) { return; }
    if (Spec.Kind == EFGBanditKind::Dynamiter)
    {
        Game->SpawnDynamite(MuzzleLocation());
        return;
    }
    Game->SpawnEnemyShot(MuzzleLocation(), bFast);
}

void AFGBandit::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!Game.IsValid()) { return; }
    StateTime += DeltaTime;
    Age += DeltaTime;
    const bool bRider = Spec.Kind == EFGBanditKind::Rider;

    // Red flash at the barrel, quicker as the shot gets nearer.
    const float Warn = Warning();
    const bool bBlinkOn = Warn > 0.0f && FMath::Fmod(Age * (5.0f + 9.0f * Warn), 1.0f) < 0.55f;
    WarnLight->SetIntensity(bBlinkOn ? 2500.0f : 0.0f);
    WarnLight->SetVisibility(bBlinkOn);        // an unlit light still costs a light

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
        if (bRider && Horse)
        {
            // The horse carries on without him and peels away.
            Horse->AddWorldOffset(FVector(-DeadVelocity.X * DeltaTime * 0.9f, Side * 260.0f * DeltaTime, 0.0f));
        }
        SetActorLocation((FTransform(Local) * AnchorTransform()).GetLocation());
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
            // Never from behind or off screen: a shot the player could not see is not dodgeable.
            FVector Chest, Head;
            AimPoints(Chest, Head);
            if (!Game->PlayerCanSee(Chest) || !Game->RequestAttackToken()) { break; }
            bHasToken = true;
            bShotThisTelegraph = false;
            if (bRider) { Play(Side > 0.0f ? TEXT("ride_aim_left") : TEXT("ride_aim_right")); }
            else if (Spec.Kind == EFGBanditKind::Dynamiter) { Play(TEXT("throw")); }
            else if (Spec.Mesh == TEXT("SK_Rifleman")) { Play(TEXT("rifle_aim_start")); }
            else if (Spec.Mesh == TEXT("SK_Heavy")) { Play(TEXT("rifle_aim_start")); }
            else { Play(TEXT("aim_start")); }
            Game->OnTelegraph(this);
            SetState(EFGBanditState::Telegraph);
        }
        break;

    case EFGBanditState::Telegraph:
        if (StateTime >= TelegraphSeconds && !bShotThisTelegraph)
        {
            bShotThisTelegraph = true;
            FireAtPlayer(false);
            if (bRider) { Play(Side > 0.0f ? TEXT("ride_shoot_left") : TEXT("ride_shoot_right")); }
            else if (Spec.Kind == EFGBanditKind::Dynamiter) { }
            else if (Spec.Mesh == TEXT("SK_Rifleman")) { Play(TEXT("rifle_shoot")); }
            else if (Spec.Mesh == TEXT("SK_Heavy")) { Play(TEXT("shotgun_shoot")); }
            else { Play(TEXT("shoot")); }
        }
        if (StateTime >= TelegraphSeconds + 0.8f)
        {
            if (bHasToken) { Game->ReleaseAttackToken(); bHasToken = false; }
            if (bRider) { Play(TEXT("ride_gallop"), true); }
            else if (Spec.Kind == EFGBanditKind::Boarder) { Play(TEXT("cover_idle"), true); }
            else { Play(Spec.Mesh == TEXT("SK_Rifleman") || Spec.Mesh == TEXT("SK_Heavy") ? TEXT("rifle_idle") : TEXT("idle"), true); }
            Timer = FMath::FRandRange(1.6f, 3.4f) * Game->FireDelayScale();
            SetState(EFGBanditState::Idle);
        }
        break;

    case EFGBanditState::Leaving:
        Local.X -= (bRider ? 1500.0f : 0.0f) * DeltaTime;
        if (!bRider) { Local.Z -= 400.0f * DeltaTime; }
        if (StateTime > 3.5f) { Destroy(); return; }
        break;

    case EFGBanditState::Scripted:
        if (DrawTimer >= 0.0f)
        {
            DrawTimer -= DeltaTime;
            if (DrawTimer < 0.0f)
            {
                Play(TEXT("shoot"));
                FireAtPlayer(true);
            }
        }
        break;

    default:
        break;
    }

    FVector Shown = Local;
    if (bRider)
    {
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
        FacePlayer();
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
    if (bHasToken && Game.IsValid()) { Game->ReleaseAttackToken(); bHasToken = false; }
    const bool bRider = Spec.Kind == EFGBanditKind::Rider;
    Play(bRider ? TEXT("ride_death") : TEXT("death_back"));
    if (bRider)
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
    DeadVelocity = FVector::ZeroVector;
    SetState(EFGBanditState::Dead);
    if (Game.IsValid()) { Game->OnBanditKilled(this); }
}
