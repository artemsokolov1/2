// Мини-футбол 5×5 — синтез и микширование звуков (см. SoccerAudio.h).

#include "SoccerAudio.h"

namespace
{
constexpr float TwoPi = 2.f * PI;
constexpr float SR = (float)FSoccerAudio::SampleRate;

int32 NumSamples(float Seconds)
{
	return FMath::CeilToInt(Seconds * SR);
}

// Привести пик звука к 1 (громкость потом задаётся множителем)
void Normalize(TArray<float>& A)
{
	float Peak = 0.f;
	for (const float X : A)
	{
		Peak = FMath::Max(Peak, FMath::Abs(X));
	}
	if (Peak > 0.f)
	{
		for (float& X : A)
		{
			X /= Peak;
		}
	}
}

// Свисток судьи: тон ~2.75 кГц с быстрой «трелью» от горошины и немного дыхания
void AddWhistle(TArray<float>& A, float StartSec, float Seconds, FRandomStream& Rng)
{
	const int32 Start = FMath::RoundToInt(StartSec * SR);
	const int32 N = NumSamples(Seconds);
	if (A.Num() < Start + N)
	{
		A.SetNumZeroed(Start + N);
	}
	float Phase = 0.f;
	for (int32 i = 0; i < N; ++i)
	{
		const float T = i / SR;
		const float Env = FMath::Min(1.f, T / 0.015f) * FMath::Clamp((Seconds - T) / 0.05f, 0.f, 1.f);
		const float Warble = FMath::Sin(TwoPi * 34.f * T);
		Phase += TwoPi * (2750.f + 110.f * Warble) / SR;
		const float Tone = FMath::Sin(Phase) + 0.2f * FMath::Sin(2.f * Phase);
		A[Start + i] += Env * (Tone * (0.8f + 0.2f * Warble) + 0.12f * Rng.FRandRange(-1.f, 1.f));
	}
}
} // namespace

float FSoccerAudio::FCrowdNoise::Next(FRandomStream& InRng, float Brightness)
{
	// Розовый шум (фильтр Пола Келлета) -> два фильтра НЧ (глухость) -> срез низкого гула
	const float W = InRng.FRandRange(-1.f, 1.f);
	Pink[0] = 0.99765f * Pink[0] + W * 0.0990460f;
	Pink[1] = 0.96300f * Pink[1] + W * 0.2965164f;
	Pink[2] = 0.57000f * Pink[2] + W * 1.0526913f;
	const float PinkNoise = Pink[0] + Pink[1] + Pink[2] + W * 0.1848f;
	Lp1 += (PinkNoise - Lp1) * Brightness;
	Lp2 += (Lp1 - Lp2) * Brightness;
	Hp += (Lp2 - Hp) * 0.04f;
	return (Lp2 - Hp) / (0.45f + 0.95f * Brightness);
}

void FSoccerAudio::Init()
{
	Rng.Initialize(20240917);
	auto Clip = [this](ESoccerSound Sound) -> TArray<float>& { return Clips[(int32)Sound]; };

	// Удар: глухой «пум» — тон с падающей частотой и короткий щелчок
	{
		TArray<float>& A = Clip(ESoccerSound::Kick);
		A.SetNumZeroed(NumSamples(0.16f));
		float Phase = 0.f;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			const float T = i / SR;
			Phase += TwoPi * (65.f + 120.f * FMath::Exp(-T * 40.f)) / SR;
			A[i] = FMath::Sin(Phase) * FMath::Exp(-T * 30.f) + Rng.FRandRange(-1.f, 1.f) * 0.5f * FMath::Exp(-T * 350.f);
		}
	}
	// Касание: короче и выше
	{
		TArray<float>& A = Clip(ESoccerSound::Touch);
		A.SetNumZeroed(NumSamples(0.08f));
		float Phase = 0.f;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			const float T = i / SR;
			Phase += TwoPi * (110.f + 160.f * FMath::Exp(-T * 60.f)) / SR;
			A[i] = FMath::Sin(Phase) * FMath::Exp(-T * 55.f) + Rng.FRandRange(-1.f, 1.f) * 0.35f * FMath::Exp(-T * 400.f);
		}
	}
	// Свистки
	AddWhistle(Clip(ESoccerSound::Whistle), 0.f, 0.4f, Rng);
	AddWhistle(Clip(ESoccerSound::WhistleLong), 0.f, 1.1f, Rng);
	AddWhistle(Clip(ESoccerSound::WhistleEnd), 0.f, 0.3f, Rng);
	AddWhistle(Clip(ESoccerSound::WhistleEnd), 0.45f, 0.3f, Rng);
	AddWhistle(Clip(ESoccerSound::WhistleEnd), 0.9f, 1.3f, Rng);

	// Штанга: металлический звон — несколько негармоничных обертонов
	{
		TArray<float>& A = Clip(ESoccerSound::Post);
		A.SetNumZeroed(NumSamples(0.9f));
		const float Freq[4] = { 620.f, 1480.f, 2390.f, 3710.f };
		const float Amp[4] = { 1.f, 0.6f, 0.4f, 0.25f };
		const float Decay[4] = { 5.f, 8.f, 12.f, 18.f };
		for (int32 i = 0; i < A.Num(); ++i)
		{
			const float T = i / SR;
			float S = Rng.FRandRange(-1.f, 1.f) * 0.6f * FMath::Exp(-T * 300.f);
			for (int32 k = 0; k < 4; ++k)
			{
				S += Amp[k] * FMath::Sin(TwoPi * Freq[k] * T) * FMath::Exp(-Decay[k] * T);
			}
			A[i] = S;
		}
	}
	// Сетка: шуршащий всплеск шума
	{
		TArray<float>& A = Clip(ESoccerSound::Net);
		A.SetNumZeroed(NumSamples(0.4f));
		float L1 = 0.f, L2 = 0.f;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			const float T = i / SR;
			L1 += (Rng.FRandRange(-1.f, 1.f) - L1) * 0.35f;
			L2 += (L1 - L2) * 0.03f;
			A[i] = (L1 - L2) * FMath::Min(1.f, T / 0.02f) * FMath::Exp(-T * 9.f);
		}
	}
	// Борт: глухой деревянный удар
	{
		TArray<float>& A = Clip(ESoccerSound::Board);
		A.SetNumZeroed(NumSamples(0.25f));
		float L = 0.f;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			const float T = i / SR;
			L += (Rng.FRandRange(-1.f, 1.f) - L) * 0.15f;
			A[i] = FMath::Sin(TwoPi * 85.f * T) * FMath::Exp(-T * 22.f) + L * 1.5f * FMath::Exp(-T * 45.f);
		}
	}
	// Рёв трибун после гола: яркий шум толпы, нарастает и медленно стихает
	{
		TArray<float>& A = Clip(ESoccerSound::Roar);
		A.SetNumZeroed(NumSamples(4.5f));
		FCrowdNoise Noise;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			const float T = i / SR;
			const float Env = FMath::Min(1.f, T / 0.35f) * (T < 1.6f ? 1.f : FMath::Exp(-(T - 1.6f) * 0.9f));
			A[i] = Noise.Next(Rng, 0.3f) * Env * (1.f + 0.25f * FMath::Sin(TwoPi * 0.8f * T));
		}
	}
	// «У-у-у»: шум толпы через резонатор, тон которого плавно опускается
	{
		TArray<float>& A = Clip(ESoccerSound::Ooh);
		A.SetNumZeroed(NumSamples(1.5f));
		FCrowdNoise Noise;
		const float R = 0.985f;
		float Y1 = 0.f, Y2 = 0.f;
		for (int32 i = 0; i < A.Num(); ++i)
		{
			const float T = i / SR;
			const float W = TwoPi * (470.f - 120.f * FMath::Min(1.f, T / 1.2f)) / SR;
			const float X = Noise.Next(Rng, 0.35f);
			const float Y = (1.f - R) * X + 2.f * R * FMath::Cos(W) * Y1 - R * R * Y2;
			Y2 = Y1;
			Y1 = Y;
			const float Env = FMath::Min(1.f, T / 0.2f) * FMath::Exp(-FMath::Max(0.f, T - 0.5f) * 2.5f);
			A[i] = (Y * 0.5f + X * 0.3f) * Env;
		}
	}

	// Пик каждого звука — 1, дальше громкость задаётся множителем
	const float Gains[(int32)ESoccerSound::Count] = {
		0.6f,  // Kick
		0.25f, // Touch
		0.35f, // Whistle
		0.35f, // WhistleLong
		0.35f, // WhistleEnd
		0.45f, // Post
		0.6f,  // Net
		0.4f,  // Board
		0.9f,  // Roar
		0.8f,  // Ooh
	};
	for (int32 i = 0; i < (int32)ESoccerSound::Count; ++i)
	{
		Normalize(Clips[i]);
		ClipGain[i] = Gains[i];
	}
	bReady = true;
}

void FSoccerAudio::Play(ESoccerSound Sound, float Gain)
{
	if (!bReady || Sound == ESoccerSound::Count || Gain <= 0.f) return;
	if (Voices.Num() >= 24)
	{
		Voices.RemoveAt(0); // слишком много звуков сразу — самый старый обрывается
	}
	FVoice Voice;
	Voice.Sound = (int32)Sound;
	Voice.Gain = ClipGain[Voice.Sound] * FMath::Clamp(Gain, 0.f, 1.5f);
	Voices.Add(Voice);
}

void FSoccerAudio::Render(int16* Out, int32 Num)
{
	if (Num <= 0) return;
	Mix.SetNumZeroed(Num);
	for (float& X : Mix)
	{
		X = 0.f;
	}

	if (bReady)
	{
		// Короткие звуки
		for (int32 v = Voices.Num() - 1; v >= 0; --v)
		{
			FVoice& Voice = Voices[v];
			const TArray<float>& Data = Clips[Voice.Sound];
			const int32 Count = FMath::Min(Num, Data.Num() - Voice.Pos);
			for (int32 i = 0; i < Count; ++i)
			{
				Mix[i] += Data[Voice.Pos + i] * Voice.Gain;
			}
			Voice.Pos += Count;
			if (Voice.Pos >= Data.Num())
			{
				Voices.RemoveAt(v);
			}
		}

		// Гул трибун: громче и ярче, когда мяч у ворот, с медленными «волнами»
		for (int32 i = 0; i < Num; ++i)
		{
			Excitement += (ExcitementTarget - Excitement) * 0.00005f;
			if (--SwellCounter <= 0)
			{
				SwellCounter = SampleRate / 2;
				SwellTarget = Rng.FRandRange(-1.f, 1.f);
			}
			Swell += (SwellTarget - Swell) * 0.0001f;
			const float Bed = Crowd.Next(Rng, 0.1f + 0.2f * Excitement);
			Mix[i] += Bed * (0.045f + 0.075f * Excitement) * (1.f + 0.3f * Swell) * CrowdLevel;
		}
	}

	const float Master = FMath::Clamp(Volume, 0.f, 1.f);
	for (int32 i = 0; i < Num; ++i)
	{
		const float S = FMath::Clamp(Mix[i] * Master, -1.f, 1.f);
		Out[i] = (int16)FMath::RoundToInt(S * 32000.f);
	}
}
