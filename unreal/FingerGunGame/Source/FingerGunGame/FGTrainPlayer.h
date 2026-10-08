#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "FGNetTypes.h"
#include "FGTrackerInput.h"
#include "FingerGunPlayerCharacter.h"
#include "FGTrainPlayer.generated.h"

class USkeletalMeshComponent;
class USpringArmComponent;
class AFGGameState;
class AFGPlayerState;
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
 *
 * Co-op: each player's own tracker drives only their own copy. Its body (lean, duck, aim) goes to the host and on to
 * the partner, who sees a cowboy kneeling on the roof. Shots are shown at once on the shooter's screen and decided
 * on the host.
 */
UCLASS()
class FINGERGUNGAME_API AFGTrainPlayer : public AFingerGunPlayerCharacter
{
    GENERATED_BODY()

public:
    AFGTrainPlayer();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

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
    /** Under a low roof the view is held at least this far down, ducking or not, so it cannot go through the ceiling. */
    float ForcedDropCm = 0.0f;
    float ForcedDropNow = 0.0f;

    FVector HeadLocation() const;

    void SetWeapon(int32 Index);
    const FFGWeapon& Weapon() const;

    UPROPERTY(ReplicatedUsing = OnRep_WeaponIndex)
    int32 WeaponIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Finger Gun|Components")
    TObjectPtr<UStaticMeshComponent> LongGun;

    /** Lose a hat. Returns true if that was the last one. Host only. */
    bool TakeHit();
    void SetHats(int32 NewHats);
    void SetSeat(const FVector& Seat, float Yaw);

    /** A new lap's gun (or ride again): refill on the player's own machine, even when it is the same gun as before. */
    UFUNCTION(Client, Reliable)
    void ClientNewGun(int32 Index);

    void PlayGun(const FString& Action, bool bLoop = false, int32 Gun = 0);

    UPROPERTY(ReplicatedUsing = OnRep_Hats)
    int32 Hats = 3;

    /** Out of hats. In co-op the partner can bring you back with a headshot. */
    UPROPERTY(Replicated)
    bool bDowned = false;

    UPROPERTY(Replicated)
    bool bGunHidden = false;

    /** Where this player stands on the roof. */
    UPROPERTY(ReplicatedUsing = OnRep_Seat)
    FVector SeatLocation = FVector::ZeroVector;

    /** Which way they face: up the train, or (in a 1v1) back down it at the other player. */
    UPROPERTY(ReplicatedUsing = OnRep_Seat)
    float SeatYaw = 0.0f;

    float HitFlash = 0.0f;      // 1 right after being hit, fades to 0
    float Rumble = 0.0f;        // 0..1, from train speed
    float HitMarker = 0.0f;     // the crosshair flashes red on a hit
    /** Where the crosshair is drawn, 0..1: the tracker's aim, pulled toward the nearest thing worth shooting. */
    FVector2D AssistedAim = FVector2D(0.5, 0.5);
    FVector2D AssistedAim2 = FVector2D(0.5, 0.5);

    /** The tracker (or mouse) on this machine for our own player, what arrived over the network for anyone else's. */
    FFGTrackerState InputState() const;

    AFGPlayerState* FGPlayerState() const;
    AFGGameState* GS() const;

    // ---- host-side bookkeeping
    float InvulnerableFor = 0.0f;
    int32 TokensIn = 0;             // bandits with this player in their sights right now
    double LastHazardS = -1.0;      // where along the line this player was last frame, for ducks and signal arms
    float ViewAspect = 16.0f / 9.0f;
    double LastShotAt = -1000.0;
    int32 LastReactionMs = -1;

    UFUNCTION(Server, Unreliable)
    void ServerInput(FFGNetInput In);

    UFUNCTION(Server, Reliable)
    void ServerAspect(float Aspect);

    UFUNCTION(Server, Reliable)
    void ServerFire(FVector_NetQuantize10 Origin, FVector_NetQuantizeNormal Dir, FVector_NetQuantize10 Muzzle, int16 ReactionMs);

    /** The partner's cowboy raises his gun (the shot itself is FGCombat's). */
    void RemoteShot();

    /** You were hit: the flash, the sound, and a buzz on your own glove. */
    UFUNCTION(Client, Reliable)
    void ClientHurt(FName Sfx);

    UFUNCTION(Client, Unreliable)
    void ClientShotResult(bool bHit);

private:
    UFUNCTION()
    void OnRep_WeaponIndex();

    UFUNCTION()
    void OnRep_Hats();

    UFUNCTION()
    void OnRep_Seat();

    /** Replicated to everyone but the owner, who has the real thing. */
    UPROPERTY(Replicated)
    FFGNetInput NetInput;

    void HandleFire(FVector2D Aim, int32 Gun);
    /** The viewmodel that stands for a gun. The first gun's hand may be the left one. */
    USkeletalMeshComponent* ModelFor(int32 Gun) const;
    void PoseGun(USkeletalMeshComponent* Model, bool bLeft, FVector2D Aim, float Down, float Kick);
    void HandleReload();
    void ApplySeat();
    void SendInput(float DeltaTime);
    void TickMagnet(AFGGameState* State);
    void TickAvatar(const FFGTrackerState& In, float DeltaTime);
    void TickAutoPlay(AFGGameState* State, float DeltaTime);

    FRotator CameraBaseRotation = FRotator::ZeroRotator;
    float Recoil = 0.0f;
    float RecoilL = 0.0f;
    float LastFireL = -1000.0f;
    float HolsterBlend = 0.0f;
    FVector2D MagnetOffset = FVector2D::ZeroVector;
    FVector2D MagnetOffset2 = FVector2D::ZeroVector;
    float InputSendTimer = 0.0f;
    float SentAspect = 0.0f;
    double SawDrawAt = -1.0;
    uint8 LastShowdownStep = 0;
    FName AvatarPose;
    double AvatarBusyUntil = 0.0;
    TWeakObjectPtr<class AFGPresentation> Presentation;
    bool bAutoPlay = false;
    float AutoTimer = 3.0f;
    int32 AutoShots = 0;
};
