import React from "react";
import {
  interpolate,
  spring,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import { THEME } from "../theme";
import { MonitorIcon, ZapIcon, VolumeIcon } from "../Icons";

export const Scene3Performance: React.FC = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();

  const titleIn = spring({
    frame: frame - 10,
    fps,
    config: { damping: 14, mass: 0.8, stiffness: 100 },
  });

  const canvasCardIn = spring({
    frame: frame - 25,
    fps,
    config: { damping: 14, mass: 0.8, stiffness: 90 },
  });

  const badgesIn = spring({
    frame: frame - 55,
    fps,
    config: { damping: 14, mass: 0.8, stiffness: 90 },
  });

  // Simulated cursor gliding across remote canvas
  const cursorX = interpolate(
    Math.sin((frame / 60) * Math.PI),
    [-1, 1],
    [320, 780]
  );
  const cursorY = interpolate(
    Math.cos((frame / 50) * Math.PI),
    [-1, 1],
    [180, 360]
  );

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
          top: "15%",
          right: "20%",
          width: 800,
          height: 800,
          borderRadius: "50%",
          background: `radial-gradient(circle, ${THEME.primaryGlow} 0%, rgba(250, 248, 245, 0) 70%)`,
          filter: "blur(60px)",
        }}
      />

      {/* Header */}
      <div
        style={{
          textAlign: "center",
          marginBottom: 36,
          opacity: Math.min(1, Math.max(0, titleIn)),
          transform: `translateY(${interpolate(titleIn, [0, 1], [20, 0], { extrapolateRight: "clamp" })}px)`,
        }}
      >
        <span
          style={{
            fontSize: 14,
            fontWeight: 700,
            letterSpacing: "0.12em",
            color: THEME.primary,
            textTransform: "uppercase",
          }}
        >
          Fluid Remote Experience
        </span>
        <h2
          style={{
            margin: "10px 0 0 0",
            fontSize: 54,
            fontWeight: 800,
            letterSpacing: "-0.03em",
            color: THEME.textPrimary,
          }}
        >
          Feels Like You're Sitting Right in Front of It.
        </h2>
      </div>

      {/* Remote Session Window Mockup */}
      <div
        style={{
          width: 1160,
          backgroundColor: THEME.bgCard,
          borderRadius: 24,
          border: `1px solid ${THEME.border}`,
          boxShadow: THEME.shadowCard,
          overflow: "hidden",
          opacity: Math.min(1, Math.max(0, canvasCardIn)),
          transform: `translateY(${interpolate(canvasCardIn, [0, 1], [30, 0], { extrapolateRight: "clamp" })}px)`,
        }}
      >
        {/* Top Session Window Toolbar */}
        <div
          style={{
            height: 54,
            backgroundColor: THEME.bgNav,
            borderBottom: `1px solid ${THEME.border}`,
            display: "flex",
            alignItems: "center",
            justifyContent: "space-between",
            padding: "0 24px",
          }}
        >
          {/* Left toolbar: Monitor tabs */}
          <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
            <div
              style={{
                display: "flex",
                alignItems: "center",
                gap: 8,
                backgroundColor: THEME.primary,
                color: "#FFF",
                padding: "6px 14px",
                borderRadius: 8,
                fontSize: 13,
                fontWeight: 600,
              }}
            >
              <MonitorIcon size={14} color="#FFF" />
              <span>Monitor 1 (Active)</span>
            </div>
            <div
              style={{
                display: "flex",
                alignItems: "center",
                gap: 8,
                backgroundColor: THEME.bgCard,
                color: THEME.textSecondary,
                border: `1px solid ${THEME.border}`,
                padding: "6px 14px",
                borderRadius: 8,
                fontSize: 13,
                fontWeight: 600,
              }}
            >
              <MonitorIcon size={14} color={THEME.textSecondary} />
              <span>Monitor 2</span>
            </div>
          </div>

          {/* Center: Live stream indicator */}
          <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
            <div style={{ width: 8, height: 8, borderRadius: "50%", backgroundColor: THEME.success }} />
            <span style={{ fontSize: 13, fontWeight: 600, color: THEME.textPrimary }}>
              60 FPS • Full HD Stream
            </span>
          </div>

          {/* Right toolbar controls */}
          <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
            {["Fit to Window", "Full Screen"].map((btn, idx) => (
              <span
                key={idx}
                style={{
                  fontSize: 13,
                  fontWeight: 600,
                  backgroundColor: THEME.bgCard,
                  border: `1px solid ${THEME.border}`,
                  padding: "6px 12px",
                  borderRadius: 8,
                  color: THEME.textSecondary,
                }}
              >
                {btn}
              </span>
            ))}
            <div
              style={{
                display: "flex",
                alignItems: "center",
                gap: 6,
                fontSize: 13,
                fontWeight: 600,
                backgroundColor: THEME.bgCard,
                border: `1px solid ${THEME.border}`,
                padding: "6px 12px",
                borderRadius: 8,
                color: THEME.textSecondary,
              }}
            >
              <VolumeIcon size={14} color={THEME.textSecondary} />
              <span>Audio On</span>
            </div>
          </div>
        </div>

        {/* Remote Screen Canvas */}
        <div
          style={{
            height: 380,
            backgroundColor: "#22201E",
            position: "relative",
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            overflow: "hidden",
          }}
        >
          {/* Subtle desktop wallpaper grid */}
          <div
            style={{
              position: "absolute",
              width: "100%",
              height: "100%",
              background: "radial-gradient(circle at center, #35302B 0%, #1A1816 100%)",
            }}
          />

          {/* Simulated remote windows / app cards */}
          <div
            style={{
              width: 580,
              height: 260,
              backgroundColor: "rgba(255, 255, 255, 0.08)",
              backdropFilter: "blur(12px)",
              borderRadius: 16,
              border: "1px solid rgba(255, 255, 255, 0.12)",
              padding: "24px",
              color: "#FFF",
              zIndex: 2,
            }}
          >
            <div style={{ fontSize: 18, fontWeight: 700, marginBottom: 8, color: "#FFF" }}>
              Work Anywhere Without Boundaries
            </div>
            <div style={{ fontSize: 14, color: "#CCC", lineHeight: 1.5 }}>
              Responsive keyboard input, smooth pointer movements, and crisp multi-monitor switching.
              Enjoy lag-free remote computing from home, the office, or on the go.
            </div>
          </div>

          {/* Simulated cursor */}
          <div
            style={{
              position: "absolute",
              left: cursorX,
              top: cursorY,
              zIndex: 10,
              pointerEvents: "none",
              display: "flex",
              alignItems: "center",
              gap: 8,
            }}
          >
            <svg width="24" height="24" viewBox="0 0 24 24" fill="none">
              <path
                d="M4 2L18 10L11 12L8 19L4 2Z"
                fill="#FFF"
                stroke={THEME.primary}
                strokeWidth="2"
              />
            </svg>
            <span
              style={{
                fontSize: 11,
                fontWeight: 700,
                backgroundColor: THEME.primary,
                color: "#FFF",
                padding: "2px 6px",
                borderRadius: 4,
              }}
            >
              Remote Cursor
            </span>
          </div>
        </div>
      </div>

      {/* Feature cards below canvas */}
      <div
        style={{
          marginTop: 28,
          display: "flex",
          gap: 20,
          opacity: Math.min(1, Math.max(0, badgesIn)),
          transform: `translateY(${interpolate(badgesIn, [0, 1], [15, 0], { extrapolateRight: "clamp" })}px)`,
        }}
      >
        {[
          { Icon: MonitorIcon, title: "Multi-Monitor Switching", desc: "Toggle between displays with 1 click" },
          { Icon: ZapIcon, title: "Instant Response", desc: "No noticeable delay while typing or clicking" },
          { Icon: VolumeIcon, title: "Full Audio Streaming", desc: "Hear system sounds and voice communication" },
        ].map((feat, idx) => (
          <div
            key={idx}
            style={{
              display: "flex",
              alignItems: "center",
              gap: 12,
              padding: "12px 22px",
              backgroundColor: THEME.bgCard,
              borderRadius: 14,
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
              <feat.Icon size={18} color={THEME.primary} strokeWidth={2} />
            </div>
            <div>
              <div style={{ fontSize: 14, fontWeight: 700, color: THEME.textPrimary }}>{feat.title}</div>
              <div style={{ fontSize: 12, color: THEME.textSecondary }}>{feat.desc}</div>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
};
