#include "FGTrain.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "FGAssets.h"
#include "FGWorldStreamer.h"

AFGTrain::AFGTrain()
{
    PrimaryActorTick.bCanEverTick = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AFGTrain::Build(const TArray<FString>& CarNames, int32 ReferenceIndex, bool bBandit)
{
    // Coupler reach per vehicle, from art/README.md.
    auto Reach = [](const FString& N, float& Front, float& Back)
    {
        if (N.Contains(TEXT("Locomotive"))) { Front = 5.4f; Back = 5.1f; }
        else if (N.Contains(TEXT("Tender"))) { Front = Back = 3.7f; }
        else if (N.Contains(TEXT("Passenger"))) { Front = Back = 7.9f; }
        else if (N.Contains(TEXT("Caboose"))) { Front = Back = 5.4f; }
        else { Front = Back = 6.0f; }
    };
    float Cursor = 0.0f;
    for (int32 i = 0; i < CarNames.Num(); ++i)
    {
        FFGTrainCar& Car = Cars.AddDefaulted_GetRef();
        Car.Mesh = TEXT("SK_Train_") + CarNames[i];
        if (bBandit && !CarNames[i].Contains(TEXT("Tender")) && !CarNames[i].Contains(TEXT("Logs")))
        {
            Car.Mesh += TEXT("_Bandit");
        }
        Reach(CarNames[i], Car.Front, Car.Back);
        Cursor -= Car.Front;
        Car.Centre = Cursor;
        Cursor -= Car.Back;
    }
    const float Shift = Cars.IsValidIndex(ReferenceIndex) ? -Cars[ReferenceIndex].Centre : 0.0f;
    for (FFGTrainCar& Car : Cars)
    {
        Car.Centre += Shift;
        USkeletalMeshComponent* Comp = NewObject<USkeletalMeshComponent>(this);
        Comp->SetMobility(EComponentMobility::Movable);
        Comp->SetSkeletalMesh(FGAssets::SkeletalMesh(TEXT("train"), Car.Mesh));
        Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Comp->SetupAttachment(GetRootComponent());
        Comp->RegisterComponent();
        Comp->bEnableUpdateRateOptimizations = true;
        if (UAnimSequence* Roll = FGAssets::Anim(TEXT("train"), Car.Mesh, TEXT("roll")))
        {
            Comp->PlayAnimation(Roll, true);
            Comp->SetPlayRate(0.0f);
        }
        Car.Comp = Comp;
        CarComps.Add(Comp);
    }
}

void AFGTrain::Place(const AFGWorldStreamer* World, float SpeedMps)
{
    // Vehicles were modelled facing -Y in Blender, which the FBX import turns into +Y. Track forward is +X.
    const FTransform MeshFix(FRotator(0.0, -90.0, 0.0), FVector(0.0, 0.0, RailTopCm));
    for (const FFGTrainCar& Car : Cars)
    {
        if (!Car.Comp) { continue; }
        Car.Comp->SetWorldTransform(MeshFix * World->TrackWorld(Offset + Car.Centre, LateralCm));
        if (!FMath::IsNearlyEqual(SpeedMps, LastSpeed, 0.05f))
        {
            // 24 frames at 30 fps is one turn of a ~1.0 m wheel: 3.1 m a turn.
            Car.Comp->SetPlayRate(SpeedMps / 3.1f * 0.8f);
        }
    }
    LastSpeed = SpeedMps;
}
