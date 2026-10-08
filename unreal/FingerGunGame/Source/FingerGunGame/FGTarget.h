#pragma once

#include "CoreMinimal.h"
#include "FGNetTypes.h"
#include "GameFramework/Actor.h"
#include "FGTarget.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class AFGPresentation;
class AFGTrainPlayer;

/**
 * Anything shootable that is not a bandit: bottles, cans, the station bell, barrels, dynamite in the air.
 * The host owns it (what happens when it is shot); every machine places it the same way: where it was put, riding on a
 * car, or flying on a throw worked out from when it was thrown.
 */
UCLASS()
class FINGERGUNGAME_API AFGTarget : public AActor
{
    GENERATED_BODY()

public:
    AFGTarget();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Mesh;

    UPROPERTY(ReplicatedUsing = OnRep_Look)
    TObjectPtr<UStaticMesh> MeshAsset;

    UPROPERTY(ReplicatedUsing = OnRep_Look)
    float Scale = 1.0f;

    UPROPERTY(Replicated)
    float Radius = 25.0f;

    UPROPERTY(Replicated)
    bool bActive = true;

    bool bBreaks = true;

    UPROPERTY(Replicated)
    FVector CentreOffset = FVector::ZeroVector;

    /** Host only: (this, who shot it). */
    TFunction<void(AFGTarget*, AFGTrainPlayer*)> OnShot;

    // Thrown things: Origin + Velocity t - g t^2 / 2 from ThrownAt.
    UPROPERTY(Replicated)
    FVector Velocity = FVector::ZeroVector;

    UPROPERTY(Replicated)
    float Gravity = 0.0f;

    UPROPERTY(Replicated)
    double ThrownAt = 0.0;

    float Fuse = -1.0f;
    TFunction<void(AFGTarget*)> OnFuse;

    /** Rides on something that moves (a car): world position = Local in the anchor's frame. */
    UPROPERTY(Replicated)
    FFGAnchor Anchor;

    UPROPERTY(Replicated)
    FVector Local = FVector::ZeroVector;

    /** Where it stands (or was thrown from) when it rides on nothing. */
    UPROPERTY(Replicated)
    FVector Origin = FVector::ZeroVector;

    UPROPERTY(Replicated)
    FRotator Facing = FRotator::ZeroRotator;

    UPROPERTY(Replicated)
    bool bExplosive = false;

    void SetMesh(UStaticMesh* Asset, float InScale);
    /** Put it somewhere fixed (and tell everyone). */
    void PlaceAt(const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator);
    /** Throw it from where it is now. */
    void Throw(const FVector& InVelocity, float InGravity);

    FVector Centre() const { return GetActorLocation() + CentreOffset; }
    /** Host only. */
    void Shot(AFGTrainPlayer* By);

private:
    UFUNCTION()
    void OnRep_Look();

    TWeakObjectPtr<AFGPresentation> Presentation;
    double Now() const;
};
