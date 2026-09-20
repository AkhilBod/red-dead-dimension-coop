#include "BanditEnemyBase.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "TimerManager.h"


ABanditEnemyBase::ABanditEnemyBase()
{
    PrimaryActorTick.bCanEverTick = false;


    // ============================================================
    // ROOT
    // ============================================================

    Root =
        CreateDefaultSubobject<USceneComponent>(
            TEXT("Root")
        );

    SetRootComponent(Root);


    // ============================================================
    // MESH
    // ============================================================

    EnemyMesh =
        CreateDefaultSubobject<UStaticMeshComponent>(
            TEXT("EnemyMesh")
        );

    EnemyMesh->SetupAttachment(Root);

    EnemyMesh->SetCollisionEnabled(
        ECollisionEnabled::QueryAndPhysics
    );

    EnemyMesh->SetCollisionResponseToAllChannels(
        ECR_Block
    );
}


void ABanditEnemyBase::BeginPlay()
{
    Super::BeginPlay();

    CurrentHealth =
        MaxHealth;

    bIsDead =
        false;
}


// ================================================================
// DAMAGE
// ================================================================

float ABanditEnemyBase::TakeDamage(
    float DamageAmount,
    const FDamageEvent& DamageEvent,
    AController* EventInstigator,
    AActor* DamageCauser)
{
    if (bIsDead)
    {
        return 0.0f;
    }


    if (DamageAmount <= 0.0f)
    {
        return 0.0f;
    }


    const float ActualDamage =
        FMath::Min(
            DamageAmount,
            CurrentHealth
        );


    CurrentHealth -=
        ActualDamage;


    CurrentHealth =
        FMath::Clamp(
            CurrentHealth,
            0.0f,
            MaxHealth
        );


    OnDamaged(
        ActualDamage,
        CurrentHealth
    );


    if (CurrentHealth <= 0.0f)
    {
        Die();
    }


    return ActualDamage;
}


// ================================================================
// HEAL
// ================================================================

void ABanditEnemyBase::Heal(float Amount)
{
    if (bIsDead)
    {
        return;
    }


    if (Amount <= 0.0f)
    {
        return;
    }


    CurrentHealth =
        FMath::Clamp(
            CurrentHealth + Amount,
            0.0f,
            MaxHealth
        );
}


// ================================================================
// HEALTH GETTERS
// ================================================================

float ABanditEnemyBase::GetHealthPercent() const
{
    if (MaxHealth <= 0.0f)
    {
        return 0.0f;
    }


    return
        CurrentHealth
        /
        MaxHealth;
}


bool ABanditEnemyBase::IsDead() const
{
    return bIsDead;
}


// ================================================================
// DEATH
// ================================================================

void ABanditEnemyBase::Die()
{
    if (bIsDead)
    {
        return;
    }


    bIsDead =
        true;


    CurrentHealth =
        0.0f;


    // Stop being hittable.
    if (EnemyMesh)
    {
        EnemyMesh->SetCollisionEnabled(
            ECollisionEnabled::NoCollision
        );
    }


    OnDied();


    if (!bDestroyOnDeath)
    {
        return;
    }


    if (DestroyDelay <= 0.0f)
    {
        Destroy();

        return;
    }


    SetLifeSpan(
        DestroyDelay
    );
}