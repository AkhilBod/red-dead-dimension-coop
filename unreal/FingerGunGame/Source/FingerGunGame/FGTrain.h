#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FGTrain.generated.h"

class AFGWorldStreamer;
class USkeletalMeshComponent;

struct FFGTrainCar
{
    FString Mesh;               // SK_Train_Boxcar
    float Front = 6.0f;         // coupler reach from the origin, metres
    float Back = 6.0f;
    float Centre = 0.0f;        // metres ahead of the train's reference point
    TObjectPtr<USkeletalMeshComponent> Comp;
};

/**
 * A consist that sits on the track. Offset is where its reference car is relative to the player
 * (0 for the player's own train), Lateral picks the main line or the side track.
 */
UCLASS()
class FINGERGUNGAME_API AFGTrain : public AActor
{
    GENERATED_BODY()

public:
    AFGTrain();

    /** Front to back. ReferenceIndex is the car that sits at Offset. */
    void Build(const TArray<FString>& CarNames, int32 ReferenceIndex, bool bBandit);

    void Place(const AFGWorldStreamer* World, float SpeedMps);

    USkeletalMeshComponent* Car(int32 Index) const { return Cars.IsValidIndex(Index) ? Cars[Index].Comp.Get() : nullptr; }
    int32 NumCars() const { return Cars.Num(); }
    float CarCentre(int32 Index) const { return Cars.IsValidIndex(Index) ? Cars[Index].Centre : 0.0f; }

    double Offset = 0.0;        // metres ahead of the player
    float LateralCm = 0.0f;

    /** Rail top is 0.53 m above ground, roof walkways about 3.9 m above that. */
    static constexpr float RailTopCm = 53.0f;
    static constexpr float RoofCm = 53.0f + 390.0f;

private:
    UPROPERTY()
    TArray<TObjectPtr<USkeletalMeshComponent>> CarComps;

    TArray<FFGTrainCar> Cars;
    float LastSpeed = -1.0f;
};
