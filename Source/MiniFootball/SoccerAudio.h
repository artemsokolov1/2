// Мини-футбол 5×5 — звук без ассетов: все звуки синтезируются в коде при запуске игры.
// Свисток судьи, удары по мячу, штанга, сетка, борт, гул трибун, рёв после гола и «у-у-у».
//
// Устройство: один бесконечный процедурный звук (USoundWaveProcedural в ASoccerGameMode),
// в который каждый кадр докладывается немного готового звука из микшера FSoccerAudio.
// Короткие звуки заранее рассчитаны в массивы и смешиваются поверх непрерывного гула трибун.

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

enum class ESoccerSound : uint8
{
	Kick,        // удар по мячу
	Touch,       // касание при ведении и приёме
	Whistle,     // короткий свисток
	WhistleLong, // длинный свисток (пенальти)
	WhistleEnd,  // финальный свисток: два коротких и длинный
	Post,        // мяч в штангу / перекладину
	Net,         // мяч в сетке
	Board,       // мяч в борт
	Roar,        // рёв трибун после гола
	Ooh,         // «у-у-у» — опасный момент
	Count
};

class FSoccerAudio
{
public:
	static constexpr int32 SampleRate = 22050;

	// Рассчитать все звуки (один раз при запуске).
	void Init();
	// Запустить короткий звук. Gain — громкость 0..1 (например, от силы удара).
	void Play(ESoccerSound Sound, float Gain = 1.f);
	// Напряжение на трибунах 0..1: мяч у ворот — гул громче и ярче.
	void SetExcitement(float Value) { ExcitementTarget = FMath::Clamp(Value, 0.f, 1.f); }
	// Смешать NumSamples сэмплов (моно, 16 бит) в Out.
	void Render(int16* Out, int32 Num);

	float Volume = 0.8f;     // общая громкость 0..1 (настройка в меню)
	float CrowdLevel = 1.f;  // громкость гула трибун (в меню — тише)

private:
	struct FVoice
	{
		int32 Sound = 0;
		int32 Pos = 0;
		float Gain = 1.f;
	};

	// Шум толпы: «розовый» шум, приглушённый фильтрами, без низкого гула. Громкость ≈ 1 (RMS).
	struct FCrowdNoise
	{
		float Pink[3] = { 0.f, 0.f, 0.f };
		float Lp1 = 0.f;
		float Lp2 = 0.f;
		float Hp = 0.f;
		float Next(FRandomStream& InRng, float Brightness); // Brightness 0.1 (глухо) .. 0.35 (ярко)
	};

	TArray<float> Clips[(int32)ESoccerSound::Count];
	float ClipGain[(int32)ESoccerSound::Count] = {};
	TArray<FVoice> Voices;
	TArray<float> Mix;
	FRandomStream Rng;
	bool bReady = false;

	// Непрерывный гул трибун
	FCrowdNoise Crowd;
	float Excitement = 0.f;
	float ExcitementTarget = 0.f;
	float Swell = 0.f;
	float SwellTarget = 0.f;
	int32 SwellCounter = 0;
};
