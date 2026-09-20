#include "FGTarget.h"

#include "Components/StaticMeshComponent.h"
#include "FGFx.h"

AFGTarget::AFGTarget()
{
    PrimaryActorTick.bCanEverTick = true;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetRootComponent(Mesh);
}

void AFGTarget::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (Follow.IsValid())
    {
        SetActorLocation(Follow->GetComponentLocation());
    }
    if (Gravity != 0.0f || !Velocity.IsZero())
    {
        Velocity.Z -= Gravity * DeltaTime;
        SetActorLocation(GetActorLocation() + Velocity * DeltaTime);
        AddActorLocalRotation(FRotator(540.0f * DeltaTime, 0.0f, 0.0f));
    }
    if (Fuse > 0.0f)
    {
        Fuse -= DeltaTime;
        if (Fuse <= 0.0f && bActive)
        {
            bActive = false;
            if (OnFuse) { OnFuse(this); }
            Destroy();
        }
    }
}

void AFGTarget::Shot()
{
    if (!bActive) { return; }
    if (bBreaks)
    {
        bActive = false;
        for (int32 i = 0; i < 7; ++i)
        {
            const FVector V(FMath::FRandRange(-250.0f, 250.0f), FMath::FRandRange(-250.0f, 250.0f), FMath::FRandRange(100.0f, 420.0f));
            AFGFx::Spawn(GetWorld(), TEXT("fx"), TEXT("SM_Shard_Glass"), FTransform(FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f), Centre(), FVector(2.0f)),
                1.2f, V, 0.0f, 980.0f, FRotator(500.0f, 300.0f, 0.0f));
        }
    }
    if (OnShot) { OnShot(this); }
    if (bBreaks) { Destroy(); }
}
