#pragma once

#include "CoreMinimal.h"
#include "BanditEnemyBase.h"
#include "Engine/NetSerialization.h"
#include "FGNetTypes.h"
#include "FGBandit.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class AFGIronHorseGameMode;
class AFGPresentation;
class AFGTrainPlayer;

USTRUCT()
struct FFGBanditSpec
{
    GENERATED_BODY()

    UPROPERTY()
    EFGBanditKind Kind = EFGBanditKind::Rider;

    UPROPERTY()
    FString Mesh = TEXT("SK_Bandit");

    /** Where it fights, cm, in the anchor's frame (+X train forward, +Y right). No anchor = the player's train at the origin. */
    UPROPERTY()
    FVector Slot = FVector::ZeroVector;

    UPROPERTY()
    FFGAnchor Anchor;

    UPROPERTY()
    float FirstShotDelay = 2.0f;

    UPROPERTY()
    float Health = 25.0f;

    UPROPERTY()
    int32 HorseCoat = 0;

    UPROPERTY()
    float Scale = 1.0f;

    /** Server time it appeared: riders sway on the same beat on every screen. */
    UPROPERTY()
    double SpawnedAt = 0.0;
};

/** Where it is in its anchor's frame and how fast that is changing, so a client can carry it on between updates. */
USTRUCT()
struct FFGBanditMotion
{
    GENERATED_BODY()

    UPROPERTY()
    FVector_NetQuantize10 Local;

    UPROPERTY()
    FVector_NetQuantize10 Velocity;

    UPROPERTY()
    double At = 0.0;
};

USTRUCT()
struct FFGAnimCmd
{
    GENERATED_BODY()

    UPROPERTY()
    FName Action;

    UPROPERTY()
    bool bLoop = false;

    UPROPERTY()
    float Rate = 1.0f;

    UPROPERTY()
    uint8 Seq = 0;          // the same clip again still counts as new
};

/**
 * Lohith's ABanditEnemyBase (health, TakeDamage, Die) wearing the imported cowboys.
 * Every shot is telegraphed for 0.7 s and needs an attack token from the game mode.
 *
 * Co-op: the host runs the bandit. Everyone else gets what it is (spec), where it is in its anchor's frame (motion),
 * what it is playing (anim), when its barrel flashes (warning) and who it is aiming at, and places it themselves.
 */
UCLASS()
class FINGERGUNGAME_API AFGBandit : public ABanditEnemyBase
{
    GENERATED_BODY()

public:
    AFGBandit();

    void Init(const FFGBanditSpec& InSpec, AFGIronHorseGameMode* InGame);

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void Die() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** Where shots are judged against. */
    void AimPoints(FVector& OutChest, FVector& OutHead) const;

    void Leave();
    bool IsRider() const { return Spec.Kind == EFGBanditKind::Rider; }
    /** Dead, here or on the host. */
    bool IsDown() const { return bIsDead || bNetDead; }
    /** Built and placed: a client may hold one for a frame before its spec arrives. */
    bool IsSpawned() const { return bBuilt; }

    /** Boss only: go for the gun. Fires after ReactionSeconds unless dead. */
    void Draw(float ReactionSeconds);

    float Play(const FString& Action, bool bLoop = false, float Rate = 1.0f);

    /** 0 when calm, 0..1 through the 0.7 s before a shot. The barrel flashes red and the HUD marks it. */
    float Warning() const;
    FVector MuzzleLocation() const;

    UPROPERTY(ReplicatedUsing = OnRep_Spec)
    FFGBanditSpec Spec;

    UPROPERTY(Replicated)
    EFGBanditState State = EFGBanditState::Entering;

    /** Who it has picked to shoot at (the HUD rings it red for them, amber for their partner). */
    UPROPERTY(Replicated)
    TObjectPtr<AFGTrainPlayer> Target;

    bool bHeadshot = false;
    /** Whose shot it was, for the kill. Host only. */
    TWeakObjectPtr<AFGTrainPlayer> LastHitBy;

    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> Body;

    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> Horse;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Cover;

    UPROPERTY()
    TObjectPtr<class UPointLightComponent> WarnLight;

private:
    UFUNCTION()
    void OnRep_Spec();

    UFUNCTION()
    void OnRep_Anim();

    UFUNCTION()
    void OnRep_Dead();

    UPROPERTY(Replicated)
    FFGBanditMotion Motion;

    UPROPERTY(ReplicatedUsing = OnRep_Anim)
    FFGAnimCmd Anim;

    UPROPERTY(ReplicatedUsing = OnRep_Dead)
    bool bNetDead = false;

    /** Server times between which the barrel flashes, closing in. 0 = calm. */
    UPROPERTY(Replicated)
    double WarnFrom = 0.0;

    UPROPERTY(Replicated)
    double WarnUntil = 0.0;

    TWeakObjectPtr<AFGIronHorseGameMode> Game;
    TWeakObjectPtr<AFGPresentation> Presentation;
    FString AnimMesh;
    bool bBuilt = false;
    bool bCosmeticDeath = false;
    float StateTime = 0.0f;
    float Timer = 0.0f;
    bool bShotThisTelegraph = false;
    TWeakObjectPtr<AFGTrainPlayer> TokenFrom;   // whose attack token it holds
    FVector Local = FVector::ZeroVector;        // current position in the anchor frame (on a client: smoothed)
    FVector DeadVelocity = FVector::ZeroVector;
    float Side = 1.0f;                          // +1 = on the player's right
    float DrawTimer = -1.0f;

    void Build();
    void PlayLocal(FName Action, bool bLoop, float Rate);
    void DieCosmetic();
    void TickHost(float DeltaTime);
    void Place(float DeltaTime);
    double Now() const;
    FTransform AnchorTransform() const;
    void SetState(EFGBanditState NewState);
    void FireAtPlayer(bool bFast);
    void ReleaseToken();
    void FacePlayers();
};
