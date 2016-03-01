// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "SteamControllerPrivatePCH.h"

DEFINE_LOG_CATEGORY_STATIC(LogSteamController, Log, All);

#if WITH_STEAM_CONTROLLER

/** Name of the current Steam SDK version in use (matches directory name) */
#define STEAM_SDK_VER TEXT("Steamv136")

// Support function to load the proper version of the Steamworks library
bool LoadSteamModule()
{
#if PLATFORM_WINDOWS
#if PLATFORM_64BITS
	void* SteamDLLHandle = nullptr;

	FString RootSteamPath = FPaths::EngineDir() / FString::Printf(TEXT("Binaries/ThirdParty/Steamworks/%s/Win64/"), STEAM_SDK_VER); 
	FPlatformProcess::PushDllDirectory(*RootSteamPath);
	SteamDLLHandle = FPlatformProcess::GetDllHandle(*(RootSteamPath + "steam_api64.dll"));
	FPlatformProcess::PopDllDirectory(*RootSteamPath);
#else
	FString RootSteamPath = FPaths::EngineDir() / FString::Printf(TEXT("Binaries/ThirdParty/Steamworks/%s/Win32/"), STEAM_SDK_VER); 
	FPlatformProcess::PushDllDirectory(*RootSteamPath);
	SteamDLLHandle = FPlatformProcess::GetDllHandle(*(RootSteamPath + "steam_api.dll"));
	FPlatformProcess::PopDllDirectory(*RootSteamPath);
#endif
#elif PLATFORM_MAC
	void* SteamDLLHandle = FPlatformProcess::GetDllHandle(TEXT("libsteam_api.dylib"));
#elif PLATFORM_LINUX
	void* SteamDLLHandle = FPlatformProcess::GetDllHandle(TEXT("libsteam_api.so"));
#endif	//PLATFORM_WINDOWS

	if (!SteamDLLHandle)
	{
		UE_LOG(LogSteamController, Warning, TEXT("Failed to load Steam library."));
		return false;
	}

	return true;
}

struct FSteamControllerState
{
	TMap<ControllerDigitalActionHandle_t, ControllerDigitalActionData_t> DigitalActionMap;
	TMap<ControllerAnalogActionHandle_t, ControllerAnalogActionData_t> AnalogActionMap;
};

class FSteamController : public IInputDevice
{

public:

	FSteamController(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler)
		: MessageHandler(InMessageHandler)
	{
		// Attempt to load the Steam Library
		if (!LoadSteamModule())
		{
			return;
		}

		// Initialize the API, so we can start calling SteamController functions
		bool bAPIInitialized = SteamAPI_Init();

			// [RCL] 2015-01-23 FIXME: move to some other code than constructor so we can handle failures more gracefully
		if (bAPIInitialized && (SteamController() != nullptr))
		{
			bool bInited = SteamController()->Init();
			UE_LOG(LogSteamController, Log, TEXT("SteamController %s initialized."), bInited ? TEXT("could not be") : TEXT("has been"));
		}
		else
		{
			UE_LOG(LogSteamController, Log, TEXT("SteamController is not available"));
		}

		InitActionHandles();
	}

	void InitActionHandles()
	{
		auto SteamControllerAPI = SteamController();
		const UInputSettings* DefaultInputSettings = GetDefault<UInputSettings>();

		if (SteamControllerAPI != nullptr && DefaultInputSettings != nullptr)
		{
			ControllerStates.Reserve(STEAM_CONTROLLER_MAX_COUNT);

			// We need to iterate through all defined actions and get their names 
			// avoiding duplicates due to actions being bound to multiple inputs
			// and store SteamController handles to the actions.

			// @HACK: Currently there is no way to fire an input event by action
			// name alone so what we do is we grab the first bound FKey to an event
			// and map the Steam controller action to it instead. Super dirty.

			TArray<FName> ActionNames;
			DefaultInputSettings->GetActionNames(ActionNames);
			for (int32 i = 0; i < ActionNames.Num(); ++i)
			{
				const FInputActionKeyMapping* ActionKeyMapping = DefaultInputSettings->ActionMappings.FindByPredicate([&](const FInputActionKeyMapping& KeyMapping)
				{
					return KeyMapping.ActionName == ActionNames[i];
				});

				if (ActionKeyMapping != nullptr)
				{
					DigitalActionFakeKeys.Add(ActionNames[i], ActionKeyMapping->Key);
				}

				DigitalActionNames.Add(ActionNames[i], SteamControllerAPI->GetDigitalActionHandle(TCHAR_TO_ANSI(*ActionNames[i].ToString())));
			}

			TArray<FName> AxisNames;
			DefaultInputSettings->GetAxisNames(AxisNames);
			for (int32 i = 0; i < AxisNames.Num(); ++i)
			{
				const FInputAxisKeyMapping* AxisKeyMapping = DefaultInputSettings->AxisMappings.FindByPredicate([&](const FInputAxisKeyMapping& AxisMapping)
				{
					return AxisMapping.AxisName == AxisNames[i];
				});

				if (AxisKeyMapping != nullptr)
				{
					AxisActionFakeKeys.Add(ActionNames[i], AxisKeyMapping->Key);
				}

				AnalogActionNames.Add(AxisNames[i], SteamControllerAPI->GetDigitalActionHandle(TCHAR_TO_ANSI(*AxisNames[i].ToString())));
			}
		}
		else
		{
			UE_LOG(LogSteamController, Log, TEXT("SteamController failed grabbing Action Handles."));
		}
	}

	FSteamControllerState* InitNewController(ControllerHandle_t ControllerHandle)
	{
		// Initialize Controller State map

		FSteamControllerState& NewState = ControllerStates.Add(ControllerHandle);

		for (auto It = DigitalActionNames.CreateConstIterator(); It; ++It)
		{
			NewState.DigitalActionMap.Add(It->Value);
		}

		for (auto It = AnalogActionNames.CreateConstIterator(); It; ++It)
		{
			NewState.AnalogActionMap.Add(It->Value);
		}

		return &NewState;
	}

	virtual ~FSteamController()
	{
		if (SteamController() != nullptr)
		{
			SteamController()->Shutdown();
		}
	}

	virtual void Tick( float DeltaTime ) override
	{
	}

	virtual void SendControllerEvents() override
	{
		auto SteamControllerAPI = SteamController();
		if (SteamControllerAPI == nullptr)
		{
			UE_LOG(LogSteamController, Error, TEXT("SteamController module failed to get SteamController API in SendControllerEvents."));
		}

		const UInputSettings* DefaultInputSettings = GetDefault<UInputSettings>();
		if (DefaultInputSettings == nullptr)
		{
			UE_LOG(LogSteamController, Error, TEXT("SteamController module failed to get Input Settings in SendControllerEvents."));
		}

		ControllerHandle_t ControllerHandles[STEAM_CONTROLLER_MAX_COUNT];
		int32 NumControllers = SteamController()->GetConnectedControllers(ControllerHandles);
		for (int32 i = 0; i < NumControllers; ++i)
		{
			FSteamControllerState* ControllerState = ControllerStates.Find(ControllerHandles[i]);
			if (ControllerState == nullptr)
			{
				ControllerState = InitNewController(ControllerHandles[i]);
			}
			if (ControllerState == nullptr)
			{
				UE_LOG(LogSteamController, Error, TEXT("SteamController connected but unable to handle its state."));
			}

			// Route Digital Action state changes as fake button presses
			// @TODO: When Epic supports triggering of Input events by action name
			// we should switch to doing that instead
			for (auto It = ControllerState->DigitalActionMap.CreateIterator(); It; ++It)
			{
				ControllerDigitalActionData_t NewActionState = SteamControllerAPI->GetDigitalActionData(ControllerHandles[i], It->Key);
				if (NewActionState.bActive && NewActionState.bState != It->Value.bState)
				{
					const FName* ActionName = DigitalActionNames.FindKey(It->Key);
					if (ActionName != nullptr)
					{
						FKey* FakeKey = DigitalActionFakeKeys.Find(*ActionName);
						if (FakeKey != nullptr)
						{
							if (NewActionState.bState)
							{
								// This is an absolute fucking hack that could be resolved
								// if the MessageHandler could fire off input events by name.
								MessageHandler->OnControllerButtonPressed(FakeKey->GetFName(), i, false);
							}
							else
							{
								MessageHandler->OnControllerButtonReleased(FakeKey->GetFName(), i, false);
							}
						}
						else
						{
							UE_LOG(LogSteamController, Error, TEXT("SteamController recieved input for action %s but this action has no key assigned to it."));
						}
					}
					else
					{
						UE_LOG(LogSteamController, Error, TEXT("SteamController recieved input for non-existant action."));
					}

					// Update our state
					It->Value = NewActionState;
				}
			}

			// @TODO: Read Analog events once Epic supports firing input events by action name
			// We can't do this easily currently as SteamController analog changes happen across
			// two axes instead of one and mapping these two axes to fake key overrides would
			// be an absolute pain in the ass			
		}
	}
	
	void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override
	{
		// Skip unless this is the large channels, which we consider to be the only SteamController feedback channels
		if ((ChannelType != FForceFeedbackChannelType::LEFT_LARGE) && (ChannelType != FForceFeedbackChannelType::RIGHT_LARGE))
		{
			return;
		}

		ControllerHandle_t ControllerHandles[STEAM_CONTROLLER_MAX_COUNT];
		int32 NumControllers = SteamController()->GetConnectedControllers(ControllerHandles);

		if ((ControllerId >= 0) && (ControllerId < NumControllers))
		{
			// Map the float values from [0,1] to be more reasonable values for the SteamController.  The docs say that [100,2000] are reasonable values
			const float Intensity = FMath::Clamp(Value * 2000.f, 0.f, 2000.f);
			if (Intensity > 0.f)
			{
				SteamController()->TriggerHapticPulse(ControllerId, ChannelType == FForceFeedbackChannelType::LEFT_LARGE ? ESteamControllerPad::k_ESteamControllerPad_Left : ESteamControllerPad::k_ESteamControllerPad_Right, Intensity);
			}
		}
	}

	void SetChannelValues(int32 ControllerId, const FForceFeedbackValues &Values) override
	{
		ControllerHandle_t ControllerHandles[STEAM_CONTROLLER_MAX_COUNT];
		int32 NumControllers = SteamController()->GetConnectedControllers(ControllerHandles);

		if ((ControllerId >= 0) && (ControllerId < NumControllers))
		{
			// Map the float values from [0,1] to be more reasonable values for the SteamController.  The docs say that [100,2000] are reasonable values
			const float LeftIntensity = FMath::Clamp(Values.LeftLarge * 2000.f, 0.f, 2000.f);
			if (LeftIntensity > 0.f)
			{
				SteamController()->TriggerHapticPulse(ControllerId, ESteamControllerPad::k_ESteamControllerPad_Left, LeftIntensity);
			}

			// Map the float values from [0,1] to be more reasonable values for the SteamController.  The docs say that [100,2000] are reasonable values
			const float RightIntensity = FMath::Clamp(Values.RightLarge * 2000.f, 0.f, 2000.f);
			if (RightIntensity > 0.f)
			{
				SteamController()->TriggerHapticPulse(ControllerId, ESteamControllerPad::k_ESteamControllerPad_Left, RightIntensity);
			}
		}
	}

	virtual void SetMessageHandler( const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler ) override
	{
		MessageHandler = InMessageHandler;
	}

	virtual bool Exec( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar ) override
	{
		return false;
	}

private:

	ControllerActionSetHandle_t ActionSetHandles[STEAM_CONTROLLER_MAX_ANALOG_ACTIONS];
	TMap<FName, ControllerDigitalActionHandle_t> DigitalActionNames;
	TMap<FName, ControllerAnalogActionHandle_t> AnalogActionNames;
	TMap<FName, FKey> DigitalActionFakeKeys;
	TMap<FName, FKey> AxisActionFakeKeys;

	TMap<ControllerHandle_t, FSteamControllerState> ControllerStates;	

	/** handler to send all messages to */
	TSharedRef<FGenericApplicationMessageHandler> MessageHandler;
};

#endif // WITH_STEAM_CONTROLLER

class FSteamControllerPlugin : public IInputDeviceModule
{
	virtual TSharedPtr< class IInputDevice > CreateInputDevice(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler) override
	{
#if WITH_STEAM_CONTROLLER
		return TSharedPtr< class IInputDevice >(new FSteamController(InMessageHandler));
#else
		return nullptr;
#endif
	}
};

IMPLEMENT_MODULE( FSteamControllerPlugin, SteamController)
