#pragma once

#include "CoreMinimal.h"
#include "FingerGunPlayerCharacter.h"
#include "FGTrainPlayer.generated.h"

class UFGTrackerInput;
class USkeletalMeshComponent;
class USpringArmComponent;
class AFGIronHorseGameMode;
class UStaticMeshComponent;

/** One gun per lap. Same finger gun, same thumb trigger: only what comes out of the barrel changes. */
struct FFGWeapon
{
    const TCHAR* Name;
    const TCHAR* Prop;          // static mesh in /Game/IronHorse/props shown instead of the revolver arm, or null
    float PropLengthCm;
    int32 Rounds;
    float Cooldown;
    float Damage;               // bandits have 25, heavies 50
    int32 MaxHits;              // how many targets one shot can take: a shotgun's spread, a rifle going through
    float AssistScale;          // on top of the game's aim assist
    int32 Tracers;
    float SfxPitch;
};

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

    /** Pivot at the standing eye. The duck is its socket offset, swept against the train, so the view can never end up inside a car. */
    UPROPERTY(VisibleAnywhere, Category = "Finger Gun|Components")
    TObjectPtr<USpringArmComponent> CameraArm;

    UPROPERTY(VisibleAnywhere, Category = "Finger Gun|Components")
    TObjectPtr<USkeletalMeshComponent> Revolver;

    /** Second revolver, mirrored, in view while a second gun hand is up. */
    UPROPERTY(VisibleAnywhere, Category = "Finger Gun|Components")
    TObjectPtr<USkeletalMeshComponent> RevolverL;

    int32 AmmoL = 6;
    float DualBlend = 0.0f;         // 0 = one gun, 1 = second gun fully up
    bool IsDual() const { return DualBlend > 0.5f; }
    bool IsEmpty() const;

    /** How far the eye drops at a full duck, cm. Roof walkway to eye is 160 standing; the roof's own clutter reaches ~50. */
    float DuckDropCm = 55.0f;
    /** Set by the game mode under a low roof: the view is held at least this far down, ducking or not, so it cannot go through the ceiling. */
    float ForcedDropCm = 0.0f;
    float ForcedDropNow = 0.0f;

    FVector HeadLocation() const;

    void SetWeapon(int32 Index);
    const FFGWeapon& Weapon() const;
    int32 WeaponIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Finger Gun|Components")
    TObjectPtr<UStaticMeshComponent> LongGun;

    /** Lose a hat. Returns true if that was the last one. */
    bool TakeHit();

    void PlayGun(const FString& Action, bool bLoop = false, int32 Gun = 0);

    int32 Hats = 3;
    int32 ShotsFired = 0;
    int32 ShotsHit = 0;
    float HitFlash = 0.0f;      // 1 right after being hit, fades to 0
    float Rumble = 0.0f;        // 0..1, from train speed
    bool bGunHidden = false;

private:
    void HandleFire(FVector2D Aim, int32 Gun);
    /** The viewmodel that stands for a gun. The first gun's hand may be the left one. */
    USkeletalMeshComponent* ModelFor(int32 Gun) const;
    void PoseGun(USkeletalMeshComponent* Model, bool bLeft, FVector2D Aim, float Down, float Kick);
    void HandleReload();

    AFGIronHorseGameMode* Game() const;
    FRotator CameraBaseRotation = FRotator::ZeroRotator;
    float Recoil = 0.0f;
    float RecoilL = 0.0f;
    float LastFireL = -1000.0f;
    float HolsterBlend = 0.0f;
};
