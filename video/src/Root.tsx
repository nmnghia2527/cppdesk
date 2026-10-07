import React from "react";
import { Composition, Folder } from "remotion";
import { MainVideo } from "./MainVideo";
import { Scene1Hero } from "./scenes/Scene1Hero";
import { Scene2Connect } from "./scenes/Scene2Connect";
import { Scene3Performance } from "./scenes/Scene3Performance";
import { Scene4PowerTools } from "./scenes/Scene4PowerTools";
import { Scene5Outro } from "./scenes/Scene5Outro";

export const RemotionRoot: React.FC = () => {
  return (
    <>
      {/* Main 30-Second 60 FPS Showcase Video */}
      <Composition
        id="CppDeskShowcase"
        component={MainVideo}
        durationInFrames={1800}
        fps={60}
        width={1920}
        height={1080}
      />

      {/* Individual Scene Previews for Studio Timeline Editing */}
      <Folder name="Scenes">
        <Composition
          id="Scene1-Hero"
          component={Scene1Hero}
          durationInFrames={375}
          fps={60}
          width={1920}
          height={1080}
        />
        <Composition
          id="Scene2-Connect"
          component={Scene2Connect}
          durationInFrames={375}
          fps={60}
          width={1920}
          height={1080}
        />
        <Composition
          id="Scene3-Performance"
          component={Scene3Performance}
          durationInFrames={375}
          fps={60}
          width={1920}
          height={1080}
        />
        <Composition
          id="Scene4-PowerTools"
          component={Scene4PowerTools}
          durationInFrames={375}
          fps={60}
          width={1920}
          height={1080}
        />
        <Composition
          id="Scene5-Outro"
          component={Scene5Outro}
          durationInFrames={420}
          fps={60}
          width={1920}
          height={1080}
        />
      </Folder>
    </>
  );
};
