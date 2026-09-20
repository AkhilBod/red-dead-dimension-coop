#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PhysicalInputComponent.generated.h"

USTRUCT(BlueprintType)
struct FPhysicalInputState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Physical Input")
    float LeanX = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Physical Input")
    float Height = 1.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Physical Input")
    bool bShoot = false;

    UPROPERTY(BlueprintReadOnly, Category = "Physical Input")
    bool bReload = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPhysicalInputEvent);

UCLASS(
    ClassGroup = (FingerGun),
    meta = (BlueprintSpawnableComponent)
)
class FINGERGUNGAME_API UPhysicalInputComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPhysicalInputComponent();

    UPROPERTY(BlueprintReadOnly, Category = "Physical Input")
    FPhysicalInputState InputState;

    UPROPERTY(BlueprintAssignable, Category = "Physical Input|Events")
    FPhysicalInputEvent OnShootPressed;

    UPROPERTY(BlueprintAssignable, Category = "Physical Input|Events")
    FPhysicalInputEvent OnReloadPressed;

    UFUNCTION(BlueprintCallable, Category = "Physical Input")
    void SetLean(float Value);

    UFUNCTION(BlueprintCallable, Category = "Physical Input")
    void SetHeight(float Value);

    UFUNCTION(BlueprintCallable, Category = "Physical Input")
    void SetShoot(bool bPressed);

    UFUNCTION(BlueprintCallable, Category = "Physical Input")
    void SetReload(bool bPressed);

    UFUNCTION(BlueprintPure, Category = "Physical Input")
    float GetLean() const;

    UFUNCTION(BlueprintPure, Category = "Physical Input")
    float GetHeight() const;

    UFUNCTION(BlueprintPure, Category = "Physical Input")
    bool IsShootPressed() const;

    UFUNCTION(BlueprintPure, Category = "Physical Input")
    bool IsReloadPressed() const;
};