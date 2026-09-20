#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FGTarget.generated.h"

class UStaticMeshComponent;

/** Anything shootable that is not a bandit: bottles, cans, the station bell, dynamite in the air. */
UCLASS()
class FINGERGUNGAME_API AFGTarget : public AActor
{
    GENERATED_BODY()

public:
    AFGTarget();
    virtual void Tick(float DeltaTime) override;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Mesh;

    float Radius = 25.0f;
    bool bActive = true;
    bool bBreaks = true;
    FVector CentreOffset = FVector::ZeroVector;
    TFunction<void(AFGTarget*)> OnShot;

    // Thrown things.
    FVector Velocity = FVector::ZeroVector;
    float Gravity = 0.0f;
    float Fuse = -1.0f;
    TFunction<void(AFGTarget*)> OnFuse;
    TWeakObjectPtr<USceneComponent> Follow;
    /** Rides on something that moves (a car of the bandit train): world position = Local in the anchor's frame. */
    TFunction<FTransform()> Anchor;
    FVector Local = FVector::ZeroVector;
    bool bExplosive = false;

    FVector Centre() const { return GetActorLocation() + CentreOffset; }
    void Shot();
};
