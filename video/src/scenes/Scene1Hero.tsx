import React from "react";
import {
  interpolate,
  spring,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
  Img,
  Easing,
} from "remotion";
import { THEME } from "../theme";
import { ZapIcon, MonitorIcon, LockIcon } from "../Icons";

export const Scene1Hero: React.FC = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();

  const logoScale = spring({
    frame: frame - 10,
    fps,
    config: { damping: 14, mass: 0.8, stiffness: 100 },
  });

  const titleProgress = spring({
    frame: frame - 22,
    fps,
    config: { damping: 14, mass: 0.8, stiffness: 100 },
  });

  const subtitleProgress = spring({
    frame: frame - 38,
    fps,
    config: { damping: 14, mass: 0.8, stiffness: 100 },
  });

  const pillsProgress = spring({
    frame: frame - 55,
    fps,
    config: { damping: 14, mass: 0.8, stiffness: 90 },
  });

  const floatY = interpolate(Math.sin((frame / 60) * Math.PI), [-1, 1], [-4, 4]);

  // Coordinated exit choreography (frames 315 - 365) to seamlessly yield to Scene 2
  // Bottom pills and subtitle glide down and fade out
  const exitBottomProgress = interpolate(frame, [315, 355], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
    easing: Easing.inOut(Easing.quad),
  });
  const exitBottomY = interpolate(exitBottomProgress, [0, 1], [0, 35]);
  const exitBottomOpacity = interpolate(exitBottomProgress, [0, 1], [1, 0]);

  // Logo & Title gracefully lift upward and dissolve, preventing any overlap with Scene 2
  const exitTopProgress = interpolate(frame, [325, 365], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
    easing: Easing.inOut(Easing.quad),
  });
  const exitTopY = interpolate(exitTopProgress, [0, 1], [0, -120]);
  const exitTopScale = interpolate(exitTopProgress, [0, 1], [1, 0.88]);
  const exitTopOpacity = interpolate(exitTopProgress, [0, 1], [1, 0]);

  return (
    <div
      style={{
        flex: 1,
        width: "100%",
        height: "100%",
        backgroundColor: THEME.bgMain,
        display: "flex",
        flexDirection: "column",
        alignItems: "center",
        justifyContent: "center",
        fontFamily: "'Segoe UI', -apple-system, BlinkMacSystemFont, sans-serif",
        position: "relative",
        overflow: "hidden",
      }}
    >
      {/* Background warm radial aura */}
      <div
        style={{
          position: "absolute",
          width: 850,
          height: 850,
          borderRadius: "50%",
          background: `radial-gradient(circle, ${THEME.primaryGlow} 0%, rgba(250, 248, 245, 0) 70%)`,
          top: "16%",
          left: "28%",
          filter: "blur(60px)",
          pointerEvents: "none",
        }}
      />

      <div
        style={{
          display: "flex",
          flexDirection: "column",
          alignItems: "center",
          transform: `translateY(${floatY}px)`,
          zIndex: 10,
        }}
      >
        {/* Top Tag Pill */}
        <div
          style={{
            opacity: interpolate(frame, [0, 20], [0, 1], { extrapolateRight: "clamp" }) * exitTopOpacity,
            transform: `translateY(${interpolate(frame, [0, 20], [12, 0], { extrapolateRight: "clamp" }) + exitTopY * 0.4}px)`,
            display: "flex",
            alignItems: "center",
            gap: 10,
            padding: "8px 22px",
            backgroundColor: THEME.bgSubtle,
            borderRadius: 999,
            border: `1px solid ${THEME.border}`,
            marginBottom: 32,
          }}
        >
          <div
            style={{
              width: 8,
              height: 8,
              borderRadius: "50%",
              backgroundColor: THEME.success,
              boxShadow: `0 0 10px ${THEME.success}`,
            }}
          />
          <span
            style={{
              fontSize: 14,
              fontWeight: 700,
              letterSpacing: "0.08em",
              color: THEME.textSecondary,
              textTransform: "uppercase",
            }}
          >
            Standalone Windows Remote Desktop
          </span>
        </div>

        {/* Logo Icon & Title Group */}
        <div
          style={{
            display: "flex",
            flexDirection: "column",
            alignItems: "center",
            transform: `translateY(${exitTopY}px) scale(${exitTopScale})`,
            opacity: exitTopOpacity,
          }}
        >
          {/* Logo Icon */}
          <div
            style={{
              transform: `scale(${Math.max(0, logoScale)})`,
              opacity: Math.min(1, Math.max(0, logoScale)),
              width: 136,
              height: 136,
              borderRadius: 34,
              backgroundColor: THEME.bgCard,
              border: `1px solid ${THEME.borderAlt}`,
              boxShadow: THEME.shadowFloat,
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
              marginBottom: 28,
            }}
          >
            <Img
              src={staticFile("icon.png")}
              style={{
                width: 102,
                height: 102,
                objectFit: "contain",
              }}
            />
          </div>

          {/* Title */}
          <h1
            style={{
              margin: 0,
              fontSize: 76,
              fontWeight: 800,
              letterSpacing: "-0.035em",
              color: THEME.textPrimary,
              opacity: Math.min(1, Math.max(0, titleProgress)),
              transform: `translateY(${interpolate(titleProgress, [0, 1], [25, 0], { extrapolateRight: "clamp" })}px)`,
            }}
          >
            CppDesk
          </h1>
        </div>

        {/* Subtitle & Value Cards Group */}
        <div
          style={{
            display: "flex",
            flexDirection: "column",
            alignItems: "center",
            transform: `translateY(${exitBottomY}px)`,
            opacity: exitBottomOpacity,
          }}
        >
          {/* Clear, user-friendly value statement */}
          <p
            style={{
              margin: "16px 0 38px 0",
              fontSize: 26,
              fontWeight: 450,
              color: THEME.textSecondary,
              maxWidth: 720,
              textAlign: "center",
              lineHeight: 1.45,
              opacity: Math.min(1, Math.max(0, subtitleProgress)),
              transform: `translateY(${interpolate(subtitleProgress, [0, 1], [20, 0], { extrapolateRight: "clamp" })}px)`,
            }}
          >
            Instant remote access to any PC, anywhere.
            <br />
            No installation required. Just download and connect.
          </p>

          {/* 3 User-Centric Value Cards */}
          <div
            style={{
              display: "flex",
              gap: 20,
              opacity: Math.min(1, Math.max(0, pillsProgress)),
              transform: `translateY(${interpolate(pillsProgress, [0, 1], [20, 0], { extrapolateRight: "clamp" })}px)`,
            }}
          >
            {[
              { label: "Zero Setup (Single 6 MB File)", Icon: ZapIcon },
              { label: "Fluid Real-Time Screen Control", Icon: MonitorIcon },
              { label: "Private & Fully Encrypted", Icon: LockIcon },
            ].map((pill, idx) => (
              <div
                key={idx}
                style={{
                  display: "flex",
                  alignItems: "center",
                  gap: 12,
                  padding: "16px 26px",
                  backgroundColor: THEME.bgCard,
                  borderRadius: 18,
                  border: `1px solid ${THEME.border}`,
                  boxShadow: THEME.shadowCard,
                }}
              >
                <div
                  style={{
                    display: "flex",
                    alignItems: "center",
                    justifyContent: "center",
                    width: 32,
                    height: 32,
                    borderRadius: 8,
                    backgroundColor: THEME.bgSubtle,
                  }}
                >
                  <pill.Icon size={18} color={THEME.primary} strokeWidth={2} />
                </div>
                <span
                  style={{
                    fontSize: 16,
                    fontWeight: 650,
                    color: THEME.textPrimary,
                  }}
                >
                  {pill.label}
                </span>
              </div>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
};
