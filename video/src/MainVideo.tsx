import React from "react";
import { useVideoConfig, Easing, Audio, Sequence, staticFile } from "remotion";
import { TransitionSeries, linearTiming } from "@remotion/transitions";
import { fade } from "@remotion/transitions/fade";

import { Scene1Hero } from "./scenes/Scene1Hero";
import { Scene2Connect } from "./scenes/Scene2Connect";
import { Scene3Performance } from "./scenes/Scene3Performance";
import { Scene4PowerTools } from "./scenes/Scene4PowerTools";
import { Scene5Outro } from "./scenes/Scene5Outro";

export const MainVideo: React.FC = () => {
  const { fps } = useVideoConfig();

  // Total duration: 375 + 375 + 375 + 375 + 420 - (4 * 30) = 1800 frames (30.00 seconds at 60 FPS)
  return (
    <>
      {/* Background ambient music track */}
      <Audio src={staticFile("audio/bgm.wav")} volume={0.16} />

      {/* Voiceover Tracks (Strictly timed after transitions have settled, zero overlap) */}
      {/* Voiceover 1 (Scene 1: 0 - 375) */}
      <Sequence from={20} durationInFrames={310}>
        <Audio src={staticFile("audio/voice_scene1.mp3")} volume={1.0} />
      </Sequence>

      {/* Voiceover 2 (Scene 2: 375 - 720, starts after Transition 1 finishes) */}
      <Sequence from={385} durationInFrames={200}>
        <Audio src={staticFile("audio/voice_scene2.mp3")} volume={1.0} />
      </Sequence>

      {/* Voiceover 3 (Scene 3: 720 - 1065, starts after Transition 2 finishes) */}
      <Sequence from={730} durationInFrames={235}>
        <Audio src={staticFile("audio/voice_scene3.mp3")} volume={1.0} />
      </Sequence>

      {/* Voiceover 4 (Scene 4: 1065 - 1410, starts after Transition 3 finishes) */}
      <Sequence from={1075} durationInFrames={245}>
        <Audio src={staticFile("audio/voice_scene4.mp3")} volume={1.0} />
      </Sequence>

      {/* Voiceover 5 (Scene 5: 1410 - 1800, starts after Transition 4 finishes) */}
      <Sequence from={1425} durationInFrames={255}>
        <Audio src={staticFile("audio/voice_scene5.mp3")} volume={1.0} />
      </Sequence>

      <TransitionSeries>
        {/* Scene 1: Brand & Instant Standalone Value (0s - ~6.2s) */}
        <TransitionSeries.Sequence durationInFrames={375} premountFor={fps}>
          <Scene1Hero />
        </TransitionSeries.Sequence>

      <TransitionSeries.Transition
        presentation={fade()}
        timing={linearTiming({ durationInFrames: 30, easing: Easing.inOut(Easing.quad) })}
      />

      {/* Scene 2: 3-Second Pairing & Direct Link (~6s - ~12.2s) */}
      <TransitionSeries.Sequence durationInFrames={375} premountFor={fps}>
        <Scene2Connect />
      </TransitionSeries.Sequence>

      <TransitionSeries.Transition
        presentation={fade()}
        timing={linearTiming({ durationInFrames: 30, easing: Easing.inOut(Easing.quad) })}
      />

      {/* Scene 3: Fluid Remote Desktop Experience (~12s - ~18.2s) */}
      <TransitionSeries.Sequence durationInFrames={375} premountFor={fps}>
        <Scene3Performance />
      </TransitionSeries.Sequence>

      <TransitionSeries.Transition
        presentation={fade()}
        timing={linearTiming({ durationInFrames: 30, easing: Easing.inOut(Easing.quad) })}
      />

      {/* Scene 4: Built-in Capabilities & Power Tools (~18s - ~24.2s) */}
      <TransitionSeries.Sequence durationInFrames={375} premountFor={fps}>
        <Scene4PowerTools />
      </TransitionSeries.Sequence>

      <TransitionSeries.Transition
        presentation={fade()}
        timing={linearTiming({ durationInFrames: 30, easing: Easing.inOut(Easing.quad) })}
      />

      {/* Scene 5: Security Permissions & Outro CTA (~24s - 30.0s) */}
      <TransitionSeries.Sequence durationInFrames={420} premountFor={fps}>
        <Scene5Outro />
      </TransitionSeries.Sequence>
    </TransitionSeries>
    </>
  );
};
