#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FGFx.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;

/** A mesh that lives briefly: muzzle flash, tracer, puff, shard, flying hat. */
UCLASS()
class FINGERGUNGAME_API AFGFx : public AActor
{
    GENERATED_BODY()

public:
    AFGFx();
    virtual void Tick(float DeltaTime) override;

    static AFGFx* Spawn(UWorld* World, const FString& Folder, const FString& Mesh, const FTransform& At, float Life,
        FVector Velocity = FVector::ZeroVector, float Grow = 0.0f, float Gravity = 0.0f, FRotator Spin = FRotator::ZeroRotator);

    void AddLight(FLinearColor Color, float Intensity, float Radius);

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Mesh;

    FVector Velocity = FVector::ZeroVector;
    FRotator Spin = FRotator::ZeroRotator;
    float Grow = 0.0f;          // scale added per second, as a fraction of the start scale
    float Gravity = 0.0f;
    float Life = 0.1f;
    float FloorZ = -100000.0f;

private:
    float Age = 0.0f;
    FVector StartScale = FVector::OneVector;
};
