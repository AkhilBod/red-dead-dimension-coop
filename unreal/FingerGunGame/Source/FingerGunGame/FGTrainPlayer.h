#pragma once

#include "CoreMinimal.h"
#include "FingerGunPlayerCharacter.h"
#include "FGTrainPlayer.generated.h"

class UFGTrackerInput;
class USkeletalMeshComponent;
class AFGIronHorseGameMode;

/**
 * Lohith's fixed first-person character (lean, crouch, crosshair, six-shooter hitscan), driven by the webcam
 * tracker instead of Enhanced Input, standing on a train roof, with a revolver in view and three hats of health.
 */
UCLASS()
class FINGERGUNGAME_API AFGTrainPlayer : public AFingerGunPlayerCharacter
{
    GENERATED_BODY()

public:
    AFGTrainPlayer();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    UPROPERTY(VisibleAnywhere, Category = "Finger Gun|Components")
    TObjectPtr<UFGTrackerInput> Tracker;

    UPROPERTY(VisibleAnywhere, Category = "Finger Gun|Components")
    TObjectPtr<USkeletalMeshComponent> Revolver;

    FVector HeadLocation() const;

    /** Lose a hat. Returns true if that was the last one. */
    bool TakeHit();

    void PlayGun(const FString& Action, bool bLoop = false);

    int32 Hats = 3;
    int32 ShotsFired = 0;
    int32 ShotsHit = 0;
    float HitFlash = 0.0f;      // 1 right after being hit, fades to 0
    float Rumble = 0.0f;        // 0..1, from train speed
    bool bGunHidden = false;

private:
    void HandleFire(FVector2D Aim);
    void HandleReload();

    AFGIronHorseGameMode* Game() const;
    FRotator CameraBaseRotation = FRotator::ZeroRotator;
    float Recoil = 0.0f;
    float HolsterBlend = 0.0f;
};
