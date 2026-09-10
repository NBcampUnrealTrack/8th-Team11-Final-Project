// Interface_Interactable.cpp

#include "Interfaces/Interface_Interactable.h"

#include "GameFramework/Actor.h"

FVector IInterface_Interactable::GetInteractionLocation_Implementation() const
{
	// 대부분의 대상은 원점이 곧 다가서는 지점이다. 어긋나는 대상만 이 함수를 덮어쓴다.
	const AActor* Self = Cast<AActor>(this);

	return Self != nullptr ? Self->GetActorLocation() : FVector::ZeroVector;
}
