// CWDecalStatics.cpp

#include "Core/CWDecalStatics.h"

#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

void UCWDecalStatics::DisableDecalReceiveOnActor(AActor* Actor)
{
	if (Actor == nullptr)
	{
		return;
	}

	// 자식 액터 컴포넌트로 붙은 메시까지 포함한다. 차량 BP 처럼 부속을 따로 달아 둔 경우가 있다.
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	Actor->GetComponents(PrimitiveComponents, true);

	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		DisableDecalReceiveOnComponent(PrimitiveComponent);
	}
}

void UCWDecalStatics::DisableDecalReceiveOnComponent(UPrimitiveComponent* Component)
{
	if (Component == nullptr)
	{
		return;
	}

	// 값이 실제로 바뀔 때만 렌더 상태를 다시 만든다. (SetReceivesDecals 안에서 걸러 준다)
	Component->SetReceivesDecals(false);
}
