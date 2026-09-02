// CWDecalStatics.h

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CWDecalStatics.generated.h"

class UPrimitiveComponent;

/**
 * 데칼 수신 여부를 만지는 유틸.
 *
 * 이 프로젝트의 데칼은 전부 바닥에 깔리는 것들이다(싱크홀 균열/구멍, 아스팔트 끈끈이, 페인트 자국).
 * 그런데 데칼은 자기 투영 박스 안에 들어온 모든 메시에 칠해지므로, 데칼 위에 선 캐릭터나
 * 그 위를 지나가는 차량에도 균열 무늬가 그대로 씌워진다.
 *
 * 해결은 "움직이는 것은 데칼을 받지 않는다" 를 못 박는 것이다.
 * UPrimitiveComponent::bReceivesDecals 는 베이스 패스에서 스텐실 비트로 처리되므로
 * 데칼 머티리얼을 고치지 않아도 되고 추가 비용도 없다.
 *
 * 생성자에서 bReceivesDecals 를 끄지 않고 런타임에 훑는 이유:
 *   1. BP 에셋에 값이 직렬화되어 있으면 C++ 기본값이 덮인다. (#109 와 같은 함정)
 *   2. BP 에디터에서 추가한 메시 컴포넌트(차량 BP 의 부속 메시, 착용물 슬롯 등)는
 *      C++ 생성자가 알 수 없다.
 * 액터 단위로 한 번 훑으면 두 경우가 모두 덮인다.
 */
UCLASS()
class CROSSWALK_API UCWDecalStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 이 액터의 모든 프리미티브 컴포넌트가 데칼을 받지 않게 한다.
	 *
	 * 컴포넌트가 다 만들어진 뒤에 불러야 한다. BeginPlay 에서 Super 를 먼저 부른 다음이면
	 * 컴포넌트 BeginPlay 가 이미 돌았으므로 런타임 생성분까지 함께 잡힌다.
	 * 그 이후에 새로 만드는 컴포넌트는 만든 자리에서 직접 꺼 줘야 한다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Rendering")
	static void DisableDecalReceiveOnActor(AActor* Actor);

	/** 컴포넌트 하나만. 런타임에 메시 컴포넌트를 새로 만드는 자리에서 쓴다. */
	UFUNCTION(BlueprintCallable, Category = "Crosswalk|Rendering")
	static void DisableDecalReceiveOnComponent(UPrimitiveComponent* Component);
};
