#include "FGCombat.h"

#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "FGAssets.h"
#include "FGBandit.h"
#include "FGFx.h"
#include "FGTarget.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace
{
    float AngleBetween(const FVector& Dir, const FVector& To)
    {
        return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, To.GetSafeNormal()), -1.0, 1.0)));
    }
}

FGCombat::FShotHit FGCombat::FindHit(UWorld* World, const FVector& Origin, const FVector& Dir, float Assist, bool bBossLocked, bool bAnyTarget)
{
    FShotHit Hit;
    Hit.Point = Origin + Dir * 6000.0f;
    if (!World) { return Hit; }
    float BestScore = 1.0f;
    for (TActorIterator<AFGBandit> It(World); It; ++It)
    {
        AFGBandit* B = *It;
        const bool bBoss = B->Spec.Kind == EFGBanditKind::Boss;
        if (!B->IsSpawned() || B->IsDown() || (bBoss && bBossLocked)) { continue; }
        FVector Chest, Head;
        B->AimPoints(Chest, Head);
        const float Dist = FVector::Dist(Origin, Chest);
        const float Allowed = FMath::Max(Assist, FMath::RadiansToDegrees(FMath::Atan(48.0f / Dist)));
        const float ChestScore = AngleBetween(Dir, Chest - Origin) / Allowed;
        const float HeadAngle = AngleBetween(Dir, Head - Origin);
        const bool bHeadHit = HeadAngle < FMath::RadiansToDegrees(FMath::Atan(27.0f / Dist)) + 0.6f;       // head and hat
        const float S = bHeadHit ? FMath::Min(ChestScore, 0.2f) : ChestScore;
        if (S < 1.0f && !bBoss) { Hit.AlsoHit.Emplace(S, B); }
        if (S < BestScore) { BestScore = S; Hit.Bandit = B; Hit.Target = nullptr; Hit.bHead = bHeadHit; Hit.Point = bHeadHit ? Head : Chest; }
    }
    for (TActorIterator<AFGTarget> It(World); It; ++It)
    {
        AFGTarget* T = *It;
        if (!T->bActive) { continue; }
        const FVector C = T->Centre();
        const float Dist = FVector::Dist(Origin, C);
        const float Allowed = FMath::Max(Assist, FMath::RadiansToDegrees(FMath::Atan(T->Radius / Dist)));
        const float S = bAnyTarget ? 0.0f : AngleBetween(Dir, C - Origin) / Allowed;
        if (S < BestScore) { BestScore = S; Hit.Target = T; Hit.Bandit = nullptr; Hit.Point = C; }
    }
    Hit.AlsoHit.Sort([](const TPair<float, AFGBandit*>& A, const TPair<float, AFGBandit*>& B) { return A.Key < B.Key; });
    if (!Hit.Any() && Dir.Z < -0.01f)
    {
        // Misses land where the ray meets the ground. (The chunks carry no collision.)
        const float Dist = -Origin.Z / Dir.Z;
        if (Dist < 30000.0f) { Hit.Point = Origin + Dir * Dist; Hit.bGround = true; }
    }
    return Hit;
}

bool FGCombat::CanSee(const FTransform& View, float HorizontalFov, float Aspect, const FVector& WorldPoint)
{
    const FVector P = View.InverseTransformPositionNoScale(WorldPoint);
    if (P.X <= 1.0f) { return false; }
    const float TanX = FMath::Tan(FMath::DegreesToRadians(HorizontalFov * 0.5f));
    const float TanY = TanX / FMath::Max(Aspect, 0.1f);
    const float ScreenX = 0.5f + 0.5f * (P.Y / P.X) / TanX;
    const float ScreenY = 0.5f - 0.5f * (P.Z / P.X) / TanY;
    return ScreenX > 0.04f && ScreenX < 0.96f && ScreenY > 0.06f && ScreenY < 0.94f;
}

void FGCombat::Sfx(UWorld* World, FName Name, float Volume, float Pitch)
{
    if (!World || World->GetNetMode() == NM_DedicatedServer) { return; }
    if (USoundBase* Sound = FGAssets::Sound(Name.ToString()))
    {
        UGameplayStatics::PlaySound2D(World, Sound, Volume, Pitch);
    }
}

void FGCombat::Fx(UWorld* World, EFGFxKind Kind, const FVector& At, float TrainSpeed)
{
    switch (Kind)
    {
    case EFGFxKind::Boom:           // an oil barrel
        if (AFGFx* Boom = AFGFx::Spawn(World, TEXT("fx"), TEXT("SM_MuzzleFlash_Big"), FTransform(FRotator::ZeroRotator, At, FVector(5.0f)), 0.35f, FVector::ZeroVector, 5.0f))
        {
            Boom->Glow(FLinearColor(3.0f, 1.4f, 0.4f), 0.4f);
            Boom->AddLight(FLinearColor(1.0f, 0.55f, 0.2f), 9000.0f, 6000.0f);
        }
        for (int32 i = 0; i < 3; ++i)
        {
            if (AFGFx* P = AFGFx::Spawn(World, TEXT("fx"), TEXT("SM_Puff_Smoke"), FTransform(FRotator(0, i * 120.0f, 0), At + FVector(0, 0, i * 60.0f), FVector(1.2f)), 1.6f, FVector(-TrainSpeed * 60.0f, 0, 260.0f), 2.2f)) { P->Glow(FLinearColor(0.12f, 0.11f, 0.10f), 0.5f); }
        }
        break;
    case EFGFxKind::DynamiteShot:   // blown up in the air
        if (AFGFx* Boom = AFGFx::Spawn(World, TEXT("fx"), TEXT("SM_MuzzleFlash_Big"), FTransform(FRotator::ZeroRotator, At, FVector(3.0f)), 0.3f, FVector::ZeroVector, 6.0f))
        {
            Boom->Glow(FLinearColor(3.0f, 1.4f, 0.4f), 0.4f);
            Boom->AddLight(FLinearColor(1.0f, 0.6f, 0.25f), 6000.0f, 5000.0f);
        }
        if (AFGFx* P = AFGFx::Spawn(World, TEXT("fx"), TEXT("SM_Puff_Smoke"), FTransform(FRotator::ZeroRotator, At, FVector(1.0f)), 1.4f, FVector(-TrainSpeed * 100.0f, 0, 100.0f), 2.5f)) { P->Glow(FLinearColor(0.12f, 0.11f, 0.10f), 0.5f); }
        break;
    case EFGFxKind::DynamiteBlast:  // the fuse ran out
        if (AFGFx* Boom = AFGFx::Spawn(World, TEXT("fx"), TEXT("SM_MuzzleFlash_Big"), FTransform(FRotator::ZeroRotator, At, FVector(4.0f)), 0.3f, FVector::ZeroVector, 6.0f))
        {
            Boom->Glow(FLinearColor(3.0f, 1.2f, 0.3f), 0.4f);
            Boom->AddLight(FLinearColor(1.0f, 0.6f, 0.25f), 8000.0f, 6000.0f);
        }
        break;
    case EFGFxKind::Dust:           // a miss kicking up the ground
        if (AFGFx* P = AFGFx::Spawn(World, TEXT("fx"), TEXT("SM_Puff_Dust"), FTransform(FRotator::ZeroRotator, At, FVector(0.5f)), 0.7f, FVector(-TrainSpeed * 100.0f, 0, 120.0f), 3.0f)) { P->Glow(FLinearColor(0.80f, 0.62f, 0.40f), 0.5f); }
        break;
    case EFGFxKind::HitDust:        // a bandit hit
        if (AFGFx* P = AFGFx::Spawn(World, TEXT("fx"), TEXT("SM_Puff_Dust"), FTransform(FRotator::ZeroRotator, At, FVector(0.25f)), 0.35f, FVector::ZeroVector, 4.0f)) { P->Glow(FLinearColor(0.80f, 0.62f, 0.40f), 0.5f); }
        break;
    case EFGFxKind::Shards:         // a bottle or a can
        for (int32 i = 0; i < 7; ++i)
        {
            const FVector V(FMath::FRandRange(-250.0f, 250.0f), FMath::FRandRange(-250.0f, 250.0f), FMath::FRandRange(100.0f, 420.0f));
            AFGFx::Spawn(World, TEXT("fx"), TEXT("SM_Shard_Glass"), FTransform(FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f), At, FVector(2.0f)),
                1.2f, V, 0.0f, 980.0f, FRotator(500.0f, 300.0f, 0.0f));
        }
        break;
    }
}

void FGCombat::PlayerShot(UWorld* World, const FVector& Muzzle, const FVector& HitPoint, int32 Tracers)
{
    const FVector ToHit = (HitPoint - Muzzle).GetSafeNormal();
    const FQuat Along = ToHit.ToOrientationQuat() * FQuat(FRotator(0.0, -90.0, 0.0));     // fx meshes point along +Y
    if (AFGFx* Flash = AFGFx::Spawn(World, TEXT("fx"), Tracers > 1 ? TEXT("SM_MuzzleFlash_Big") : TEXT("SM_MuzzleFlash_A"), FTransform(Along, Muzzle, FVector(0.6f)), 0.06f))
    {
        Flash->AddLight(FLinearColor(1.0f, 0.7f, 0.35f), 900.0f, 1500.0f);
    }
    const float TracerSpeed = 40000.0f;
    for (int32 i = 1; i < Tracers; ++i)
    {
        // pellets: the same tracer, fanned out
        const FVector Spread = (ToHit + FVector(FMath::FRandRange(-0.06f, 0.06f), FMath::FRandRange(-0.06f, 0.06f), FMath::FRandRange(-0.04f, 0.04f))).GetSafeNormal();
        AFGFx::Spawn(World, TEXT("fx"), TEXT("SM_Tracer_Player"), FTransform(Spread.ToOrientationQuat() * FQuat(FRotator(0.0, -90.0, 0.0)), Muzzle, FVector(1.0f, 1.2f, 1.0f)), 0.09f, Spread * TracerSpeed);
    }
    AFGFx::Spawn(World, TEXT("fx"), TEXT("SM_Tracer_Player"), FTransform(Along, Muzzle, FVector(1.0f, 2.0f, 1.0f)), FMath::Clamp(FVector::Dist(Muzzle, HitPoint) / TracerSpeed, 0.03f, 0.2f), ToHit * TracerSpeed);
}

void FGCombat::EnemyShot(UWorld* World, const FVector& From, const FVector& Target, float Flight)
{
    const FVector Dir = (Target - From).GetSafeNormal();
    const float Speed = FVector::Dist(From, Target) / FMath::Max(Flight, 0.05f);
    const FQuat Along = Dir.ToOrientationQuat() * FQuat(FRotator(0.0, -90.0, 0.0));
    if (AFGFx* Fx = AFGFx::Spawn(World, TEXT("fx"), TEXT("SM_Tracer_Enemy"), FTransform(Along, From, FVector(1.4f)), Flight + 0.35f, Dir * Speed))
    {
        Fx->AddLight(FLinearColor(1.0f, 0.35f, 0.15f), 400.0f, 900.0f);
    }
    if (AFGFx* Flash = AFGFx::Spawn(World, TEXT("fx"), TEXT("SM_MuzzleFlash_Big"), FTransform(Along, From, FVector(0.8f)), 0.07f))
    {
        Flash->AddLight(FLinearColor(1.0f, 0.6f, 0.3f), 1200.0f, 2500.0f);
    }
}

void FGCombat::HatOff(UWorld* World, const FVector& Head)
{
    // The first hit shoots your hat off. Everyone sees it go.
    AFGFx::Spawn(World, TEXT("props"), TEXT("SM_Hat_Deputy"), FTransform(FRotator(0, 90, 0), Head + FVector(60, 0, 25)), 3.0f,
        FVector(900.0f, FMath::FRandRange(-200.f, 200.f), 350.0f), 0.0f, 600.0f, FRotator(300.0f, 200.0f, 0.0f));
}
