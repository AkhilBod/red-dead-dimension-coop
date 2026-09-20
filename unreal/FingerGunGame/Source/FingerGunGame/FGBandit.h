#pragma once

#include "CoreMinimal.h"
#include "BanditEnemyBase.h"
#include "FGBandit.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class AFGIronHorseGameMode;

enum class EFGBanditKind : uint8 { Rider, Boarder, TrainShooter, Dynamiter, Boss };
enum class EFGBanditState : uint8 { Entering, Idle, Telegraph, Recover, Leaving, Dead, Scripted };

struct FFGBanditSpec
{
    EFGBanditKind Kind = EFGBanditKind::Rider;
    FString Mesh = TEXT("SK_Bandit");
    /** Where it fights, cm, in the anchor's frame (+X train forward, +Y right). No anchor = the player's train at the origin. */
    FVector Slot = FVector::ZeroVector;
    TFunction<FTransform()> Anchor;
    float FirstShotDelay = 2.0f;
    float Health = 25.0f;
    int32 HorseCoat = 0;
    float Scale = 1.0f;
};

/**
 * Lohith's ABanditEnemyBase (health, TakeDamage, Die) wearing the imported cowboys.
 * Every shot is telegraphed for 0.7 s and needs one of the game mode's two attack tokens.
 */
UCLASS()
class FINGERGUNGAME_API AFGBandit : public ABanditEnemyBase
{
    GENERATED_BODY()

public:
    AFGBandit();

    void Init(const FFGBanditSpec& InSpec, AFGIronHorseGameMode* InGame);

    virtual void Tick(float DeltaTime) override;
    virtual void Die() override;

    /** Where shots are judged against. */
    void AimPoints(FVector& OutChest, FVector& OutHead) const;

    void Leave();
    bool IsRider() const { return Spec.Kind == EFGBanditKind::Rider; }

    /** Boss only: go for the gun. Fires after ReactionSeconds unless dead. */
    void Draw(float ReactionSeconds);

    float Play(const FString& Action, bool bLoop = false, float Rate = 1.0f);

    /** 0 when calm, 0..1 through the 0.7 s before a shot. The barrel flashes red and the HUD marks it. */
    float Warning() const;
    FVector MuzzleLocation() const;

    FFGBanditSpec Spec;
    EFGBanditState State = EFGBanditState::Entering;
    bool bHeadshot = false;

    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> Body;

    UPROPERTY()
    TObjectPtr<USkeletalMeshComponent> Horse;

    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Cover;

    UPROPERTY()
    TObjectPtr<class UPointLightComponent> WarnLight;

private:
    TWeakObjectPtr<AFGIronHorseGameMode> Game;
    FString AnimMesh;
    float StateTime = 0.0f;
    float Timer = 0.0f;
    float Age = 0.0f;
    bool bHasToken = false;
    bool bShotThisTelegraph = false;
    FVector Local = FVector::ZeroVector;        // current position in the anchor frame
    FVector DeadVelocity = FVector::ZeroVector;
    float Side = 1.0f;                          // +1 = on the player's right
    float DrawTimer = -1.0f;

    FTransform AnchorTransform() const;
    void SetState(EFGBanditState NewState);
    void FireAtPlayer(bool bFast);
    void FacePlayer();
};
