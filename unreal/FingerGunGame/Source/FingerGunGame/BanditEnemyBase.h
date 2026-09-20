#pragma once
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BanditEnemyBase.generated.h"

class USceneComponent;
class UStaticMeshComponent;

UCLASS()
class FINGERGUNGAME_API ABanditEnemyBase : public AActor
{
    GENERATED_BODY()

public:
    ABanditEnemyBase();

protected:
    virtual void BeginPlay() override;

public:

    // ============================================================
    // COMPONENTS
    // ============================================================

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Enemy|Components"
    )
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Enemy|Components"
    )
    TObjectPtr<UStaticMeshComponent> EnemyMesh;


    // ============================================================
    // HEALTH
    // ============================================================

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Enemy|Health",
        meta = (ClampMin = "1.0")
    )
    float MaxHealth = 100.0f;

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Enemy|Health"
    )
    float CurrentHealth = 100.0f;


    // ============================================================
    // STATE
    // ============================================================

    UPROPERTY(
        VisibleAnywhere,
        BlueprintReadOnly,
        Category = "Enemy|State"
    )
    bool bIsDead = false;


    // ============================================================
    // DAMAGE
    // ============================================================

    /**
     * Unreal's built-in damage entry point.
     *
     * This works with UGameplayStatics::ApplyPointDamage()
     * from your player hitscan code.
     */
    virtual float TakeDamage(
        float DamageAmount,
        struct FDamageEvent const& DamageEvent,
        AController* EventInstigator,
        AActor* DamageCauser
    ) override;


    // ============================================================
    // HEALTH FUNCTIONS
    // ============================================================

    UFUNCTION(
        BlueprintCallable,
        Category = "Enemy|Health"
    )
    void Heal(float Amount);

    UFUNCTION(
        BlueprintPure,
        Category = "Enemy|Health"
    )
    float GetHealthPercent() const;

    UFUNCTION(
        BlueprintPure,
        Category = "Enemy|Health"
    )
    bool IsDead() const;


    // ============================================================
    // DEATH
    // ============================================================

    UFUNCTION(
        BlueprintCallable,
        Category = "Enemy|Death"
    )
    virtual void Die();


    // ============================================================
    // BLUEPRINT EVENTS
    // ============================================================

    /**
     * Called every time this enemy takes valid damage.
     *
     * Use in BP for:
     * - hit flash
     * - hit sound
     * - particles
     * - animation
     */
    UFUNCTION(
        BlueprintImplementableEvent,
        Category = "Enemy|Events"
    )
    void OnDamaged(
        float DamageAmount,
        float NewHealth
    );

    /**
     * Called when this enemy dies.
     *
     * Use in BP for:
     * - death animation
     * - particles
     * - score effects
     */
    UFUNCTION(
        BlueprintImplementableEvent,
        Category = "Enemy|Events"
    )
    void OnDied();


    // ============================================================
    // DEATH SETTINGS
    // ============================================================

    /**
     * If true, enemy destroys itself automatically after death.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Enemy|Death"
    )
    bool bDestroyOnDeath = true;

    /**
     * Delay before destruction.
     *
     * Set to 0 for instant disappearance.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Enemy|Death",
        meta = (ClampMin = "0.0")
    )
    float DestroyDelay = 0.0f;
};