#include "FGFx.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "FGAssets.h"

AFGFx::AFGFx()
{
    PrimaryActorTick.bCanEverTick = true;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCastShadow(false);
    SetRootComponent(Mesh);
}

AFGFx* AFGFx::Spawn(UWorld* World, const FString& Folder, const FString& MeshName, const FTransform& At, float Life, FVector Velocity, float Grow, float Gravity, FRotator Spin)
{
    if (!World) { return nullptr; }
    AFGFx* Fx = World->SpawnActor<AFGFx>(AFGFx::StaticClass(), At);
    if (!Fx) { return nullptr; }
    Fx->Mesh->SetStaticMesh(FGAssets::StaticMesh(Folder, MeshName));
    Fx->SetActorScale3D(At.GetScale3D());
    Fx->StartScale = At.GetScale3D();
    Fx->Life = Life;
    Fx->Velocity = Velocity;
    Fx->Grow = Grow;
    Fx->Gravity = Gravity;
    Fx->Spin = Spin;
    return Fx;
}

void AFGFx::AddLight(FLinearColor Color, float Intensity, float Radius)
{
    UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
    Light->SetMobility(EComponentMobility::Movable);
    Light->SetupAttachment(Mesh);
    Light->SetIntensityUnits(ELightUnits::Candelas);
    Light->SetIntensity(Intensity);
    Light->SetLightColor(Color);
    Light->SetAttenuationRadius(Radius);
    Light->SetCastShadows(false);
    Light->RegisterComponent();
}

void AFGFx::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    Age += DeltaTime;
    if (Age >= Life)
    {
        Destroy();
        return;
    }
    Velocity.Z -= Gravity * DeltaTime;
    FVector Loc = GetActorLocation() + Velocity * DeltaTime;
    if (Loc.Z < FloorZ)
    {
        Loc.Z = FloorZ;
        Velocity = FVector(Velocity.X, Velocity.Y, 0.0f);
        Spin = FRotator::ZeroRotator;
    }
    SetActorLocationAndRotation(Loc, GetActorRotation() + Spin * DeltaTime);
    if (Grow != 0.0f)
    {
        SetActorScale3D(StartScale * FMath::Max(0.01f, 1.0f + Grow * Age));
    }
}
