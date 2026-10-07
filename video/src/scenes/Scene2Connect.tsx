import React from "react";
import {
  interpolate,
  spring,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import { THEME } from "../theme";
import { CheckIcon, ArrowRightIcon } from "../Icons";

export const Scene2Connect: React.FC = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();

  const titleIn = spring({
    frame: frame + 4,
    fps,
    config: { damping: 16, mass: 0.8, stiffness: 90 },
  });

  const cardLeftIn = spring({
    frame: frame + 2,
    fps,
    config: { damping: 16, mass: 0.8, stiffness: 90 },
  });

  const cardRightIn = spring({
    frame: frame - 4,
    fps,
    config: { damping: 16, mass: 0.8, stiffness: 90 },
  });

  const connectionPillIn = spring({
    frame: frame - 12,
    fps,
    config: { damping: 15, mass: 0.8, stiffness: 100 },
  });

  const footerIn = spring({
    frame: frame - 22,
    fps,
    config: { damping: 16, mass: 0.8, stiffness: 90 },
  });

  // Animated digit typing with fixed container (no layout shift)
  const targetId = "482 910 375";
  const typeIndex = Math.min(
    targetId.length,
    Math.floor(interpolate(frame, [40, 100], [0, targetId.length], { extrapolateLeft: "clamp", extrapolateRight: "clamp" }))
  );
  const typedId = targetId.substring(0, typeIndex);

  // Button hover / click state
  const isConnecting = frame > 120;

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
      {/* Background ambient gradient */}
      <div
        style={{
          position: "absolute",
          top: "12%",
          width: 800,
          height: 800,
          borderRadius: "50%",
          background: `radial-gradient(circle, rgba(217, 119, 87, 0.08) 0%, rgba(250, 248, 245, 0) 70%)`,
          filter: "blur(60px)",
        }}
      />

      {/* Header section */}
      <div
        style={{
          textAlign: "center",
          marginBottom: 44,
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
          Fast & Intuitive Pairing
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
          Connect to Any Computer in 3 Seconds.
        </h2>
      </div>

      {/* Side-by-side connection cards */}
      <div
        style={{
          display: "flex",
          alignItems: "center",
          justifyContent: "center",
          gap: 48,
          width: 1180,
          position: "relative",
        }}
      >
        {/* Left Card: Your Machine */}
        <div
          style={{
            flex: 1,
            backgroundColor: THEME.bgCard,
            borderRadius: 24,
            padding: "36px 32px",
            border: `1px solid ${THEME.border}`,
            boxShadow: THEME.shadowCard,
            opacity: Math.min(1, Math.max(0, cardLeftIn)),
            transform: `translateY(${interpolate(cardLeftIn, [0, 1], [22, 0], { extrapolateRight: "clamp" })}px) scale(${interpolate(cardLeftIn, [0, 1], [0.97, 1], { extrapolateRight: "clamp" })})`,
          }}
        >
          <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", marginBottom: 20 }}>
            <span style={{ fontSize: 13, fontWeight: 700, letterSpacing: "0.1em", color: THEME.textMuted }}>
              YOUR COMPUTER (HOST)
            </span>
            <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
              <div style={{ width: 8, height: 8, borderRadius: "50%", backgroundColor: THEME.success }} />
              <span style={{ fontSize: 13, fontWeight: 600, color: THEME.success }}>Online & Ready</span>
            </div>
          </div>

          <div style={{ fontSize: 14, color: THEME.textSecondary, marginBottom: 8 }}>
            Share your 9-Digit ID to allow access
          </div>
          <div
            style={{
              fontSize: 40,
              fontWeight: 800,
              fontFamily: "monospace",
              color: THEME.primary,
              letterSpacing: "0.08em",
              backgroundColor: THEME.bgSubtle,
              padding: "16px 20px",
              borderRadius: 14,
              border: `1px solid ${THEME.borderAlt}`,
              display: "flex",
              alignItems: "center",
              justifyContent: "space-between",
            }}
          >
            <span>482 910 375</span>
            <span style={{ fontSize: 16, backgroundColor: THEME.bgCard, padding: "6px 12px", borderRadius: 8, color: THEME.textPrimary, border: `1px solid ${THEME.border}` }}>
              Copy
            </span>
          </div>

          <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginTop: 24 }}>
            <span style={{ fontSize: 14, color: THEME.textSecondary }}>One-Time Password</span>
            <span
              style={{
                fontFamily: "monospace",
                fontWeight: 700,
                fontSize: 16,
                backgroundColor: THEME.bgCardAlt,
                padding: "6px 14px",
                borderRadius: 8,
                border: `1px solid ${THEME.border}`,
                color: THEME.textPrimary,
              }}
            >
              8K9M-2X4L
            </span>
          </div>
        </div>

        {/* Central Arrow & Connect Badge */}
        <div
          style={{
            display: "flex",
            flexDirection: "column",
            alignItems: "center",
            width: 100,
            opacity: Math.min(1, Math.max(0, connectionPillIn)),
            transform: `scale(${interpolate(connectionPillIn, [0, 1], [0.85, 1], { extrapolateRight: "clamp" })})`,
          }}
        >
          <div
            style={{
              width: 52,
              height: 52,
              borderRadius: "50%",
              backgroundColor: isConnecting ? THEME.success : THEME.primary,
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
              boxShadow: `0 8px 24px ${THEME.primaryGlow}`,
              marginBottom: 10,
            }}
          >
            {isConnecting ? (
              <CheckIcon size={24} color="#FFF" strokeWidth={2.5} />
            ) : (
              <ArrowRightIcon size={24} color="#FFF" strokeWidth={2.5} />
            )}
          </div>
          <span style={{ fontSize: 13, fontWeight: 700, color: THEME.textPrimary, textAlign: "center" }}>
            {isConnecting ? "Connected" : "Direct Link"}
          </span>
        </div>

        {/* Right Card: Remote Desk */}
        <div
          style={{
            flex: 1,
            backgroundColor: THEME.bgCard,
            borderRadius: 24,
            padding: "36px 32px",
            border: `1px solid ${THEME.border}`,
            boxShadow: THEME.shadowCard,
            opacity: Math.min(1, Math.max(0, cardRightIn)),
            transform: `translateY(${interpolate(cardRightIn, [0, 1], [22, 0], { extrapolateRight: "clamp" })}px) scale(${interpolate(cardRightIn, [0, 1], [0.97, 1], { extrapolateRight: "clamp" })})`,
          }}
        >
          <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", marginBottom: 20 }}>
            <span style={{ fontSize: 13, fontWeight: 700, letterSpacing: "0.1em", color: THEME.textMuted }}>
              REMOTE DESK (VIEWER)
            </span>
            <span style={{ fontSize: 13, fontWeight: 600, color: THEME.textSecondary }}>Instant Join</span>
          </div>

          <div style={{ fontSize: 14, color: THEME.textSecondary, marginBottom: 8 }}>
            Enter remote computer ID
          </div>
          <div
            style={{
              fontSize: 34,
              fontWeight: 700,
              fontFamily: "monospace",
              color: THEME.textPrimary,
              letterSpacing: "0.06em",
              backgroundColor: THEME.bgSubtle,
              padding: "16px 20px",
              borderRadius: 14,
              border: `1px solid ${THEME.border}`,
              height: 48,
              display: "flex",
              alignItems: "center",
            }}
          >
            <span>{typedId}</span>
            <span style={{ opacity: Math.sin(frame / 6) > 0 ? 1 : 0, color: THEME.primary, marginLeft: 2 }}>|</span>
          </div>

          <div
            style={{
              marginTop: 22,
              padding: "16px",
              backgroundColor: isConnecting ? THEME.success : THEME.primary,
              borderRadius: 14,
              color: "#FFF",
              fontWeight: 700,
              fontSize: 18,
              textAlign: "center",
              boxShadow: `0 8px 24px ${THEME.primaryGlow}`,
            }}
          >
            {isConnecting ? "Session Active" : "Connect"}
          </div>
        </div>
      </div>

      {/* Feature bullets at bottom */}
      <div
        style={{
          marginTop: 40,
          opacity: Math.min(1, Math.max(0, footerIn)),
          transform: `translateY(${interpolate(footerIn, [0, 1], [15, 0], { extrapolateRight: "clamp" })}px)`,
          display: "flex",
          alignItems: "center",
          gap: 24,
        }}
      >
        <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
          <CheckIcon size={14} color={THEME.success} strokeWidth={2.5} />
          <span style={{ fontSize: 15, fontWeight: 600, color: THEME.textSecondary }}>
            Interactive Screen Approval
          </span>
        </div>
        <span style={{ fontSize: 15, fontWeight: 600, color: THEME.border }}>•</span>
        <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
          <CheckIcon size={14} color={THEME.success} strokeWidth={2.5} />
          <span style={{ fontSize: 15, fontWeight: 600, color: THEME.textSecondary }}>
            Permanent Password Option
          </span>
        </div>
        <span style={{ fontSize: 15, fontWeight: 600, color: THEME.border }}>•</span>
        <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
          <CheckIcon size={14} color={THEME.success} strokeWidth={2.5} />
          <span style={{ fontSize: 15, fontWeight: 600, color: THEME.textSecondary }}>
            Works Through Firewalls & NAT
          </span>
        </div>
      </div>
    </div>
  );
};
