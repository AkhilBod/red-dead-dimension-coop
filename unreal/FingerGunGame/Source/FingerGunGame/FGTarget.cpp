#include "FGTarget.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "FGGameState.h"
#include "FGPresentation.h"
#include "Net/UnrealNetwork.h"

AFGTarget::AFGTarget()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(false);        // placed on every machine, see Tick
    bAlwaysRelevant = true;
    SetNetUpdateFrequency(10.0f);
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetRootComponent(Mesh);
}

void AFGTarget::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AFGTarget, MeshAsset);
    DOREPLIFETIME(AFGTarget, Scale);
    DOREPLIFETIME(AFGTarget, Radius);
    DOREPLIFETIME(AFGTarget, bActive);
    DOREPLIFETIME(AFGTarget, CentreOffset);
    DOREPLIFETIME(AFGTarget, Velocity);
    DOREPLIFETIME(AFGTarget, Gravity);
    DOREPLIFETIME(AFGTarget, ThrownAt);
    DOREPLIFETIME(AFGTarget, Anchor);
    DOREPLIFETIME(AFGTarget, Local);
    DOREPLIFETIME(AFGTarget, Origin);
    DOREPLIFETIME(AFGTarget, Facing);
    DOREPLIFETIME(AFGTarget, bExplosive);
}

void AFGTarget::BeginPlay()
{
    Super::BeginPlay();
    Presentation = AFGPresentation::Get(this);
    if (Presentation.IsValid()) { AddTickPrerequisiteActor(Presentation.Get()); }
    if (HasAuthority() && Origin.IsZero())
    {
        Origin = GetActorLocation();
        Facing = GetActorRotation();
    }
}

double AFGTarget::Now() const
{
    const AFGGameState* GS = GetWorld()->GetGameState<AFGGameState>();
    return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

void AFGTarget::SetMesh(UStaticMesh* Asset, float InScale)
{
    MeshAsset = Asset;
    Scale = InScale;
    OnRep_Look();
}

void AFGTarget::OnRep_Look()
{
    Mesh->SetStaticMesh(MeshAsset);
    SetActorScale3D(FVector(Scale));
}

void AFGTarget::PlaceAt(const FVector& Location, const FRotator& Rotation)
{
    Origin = Location;
    Facing = Rotation;
    SetActorLocationAndRotation(Location, Rotation);
}

void AFGTarget::Throw(const FVector& InVelocity, float InGravity)
{
    Origin = GetActorLocation();
    Velocity = InVelocity;
    Gravity = InGravity;
    ThrownAt = Now();
}

void AFGTarget::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (Anchor.IsSet() && Presentation.IsValid())
    {
        const FTransform T = FTransform(Local) * Presentation->AnchorTransform(Anchor);
        SetActorLocationAndRotation(T.GetLocation(), T.GetRotation());
    }
    else if (Gravity != 0.0f || !Velocity.IsZero())
    {
        // The same arc on every screen, from when it left the hand.
        const float Age = float(FMath::Max(0.0, Now() - ThrownAt));
        SetActorLocationAndRotation(Origin + Velocity * Age - FVector(0.0f, 0.0f, 0.5f * Gravity * Age * Age), FRotator(540.0f * Age, 0.0f, 0.0f));
    }
    else if (!HasAuthority())
    {
        SetActorLocationAndRotation(Origin, Facing);
    }
    if (HasAuthority() && Fuse > 0.0f)
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

void AFGTarget::Shot(AFGTrainPlayer* By)
{
    if (!bActive) { return; }
    if (bBreaks)
    {
        bActive = false;
        if (AFGGameState* GS = GetWorld()->GetGameState<AFGGameState>()) { GS->MulticastFx(EFGFxKind::Shards, Centre(), 0.0f); }
    }
    if (OnShot) { OnShot(this, By); }
    if (bBreaks) { Destroy(); }
}
