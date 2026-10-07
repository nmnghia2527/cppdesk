import React from "react";
import {
  interpolate,
  spring,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import { THEME } from "../theme";
import {
  FolderIcon,
  TerminalIcon,
  ShieldIcon,
  LockIcon,
  RefreshIcon,
  KeyboardIcon,
  MicIcon,
  VolumeIcon,
  PenIcon,
  CheckIcon,
} from "../Icons";

export const Scene4PowerTools: React.FC = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();

  const titleIn = spring({
    frame: frame - 10,
    fps,
    config: { damping: 14, mass: 0.8, stiffness: 100 },
  });

  const bento1 = spring({ frame: frame - 25, fps, config: { damping: 14, mass: 0.8, stiffness: 100 } });
  const bento2 = spring({ frame: frame - 40, fps, config: { damping: 14, mass: 0.8, stiffness: 100 } });
  const bento3 = spring({ frame: frame - 55, fps, config: { damping: 14, mass: 0.8, stiffness: 100 } });
  const bento4 = spring({ frame: frame - 70, fps, config: { damping: 14, mass: 0.8, stiffness: 100 } });

  const transferProgress = interpolate(frame, [50, 160], [0, 100], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
  });

  const isComplete = frame >= 160;

  const completeBadgeSpring = spring({
    frame: frame - 160,
    fps,
    config: { damping: 11, mass: 0.5, stiffness: 180 },
  });

  const checkmarkSpring = spring({
    frame: frame - 164,
    fps,
    config: { damping: 10, mass: 0.4, stiffness: 220 },
  });

  const toastSpring = spring({
    frame: frame - 170,
    fps,
    config: { damping: 14, mass: 0.6, stiffness: 140 },
  });

  const successGlow = interpolate(frame, [160, 180, 220], [0, 12, 4], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
  });

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
      {/* Background radial accent */}
      <div
        style={{
          position: "absolute",
          top: "20%",
          left: "20%",
          width: 800,
          height: 800,
          borderRadius: "50%",
          background: `radial-gradient(circle, ${THEME.primaryGlow} 0%, rgba(250, 248, 245, 0) 70%)`,
          filter: "blur(70px)",
        }}
      />

      {/* Header */}
      <div
        style={{
          textAlign: "center",
          marginBottom: 38,
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
          Built-In Capabilities
        </span>
        <h2
          style={{
            margin: "8px 0 0 0",
            fontSize: 54,
            fontWeight: 800,
            letterSpacing: "-0.03em",
            color: THEME.textPrimary,
          }}
        >
          Everything You Need in One Place.
        </h2>
      </div>

      {/* 4-Card Bento Grid */}
      <div
        style={{
          display: "grid",
          gridTemplateColumns: "1fr 1fr",
          gridTemplateRows: "1fr 1fr",
          gap: 24,
          width: 1200,
          height: 520,
        }}
      >
        {/* Bento 1: Drag & Drop File Transfer */}
        <div
          style={{
            backgroundColor: THEME.bgCard,
            borderRadius: 20,
            padding: "28px",
            border: `1px solid ${THEME.border}`,
            boxShadow: THEME.shadowCard,
            opacity: Math.min(1, Math.max(0, bento1)),
            transform: `scale(${interpolate(bento1, [0, 1], [0.95, 1], { extrapolateRight: "clamp" })})`,
            display: "flex",
            flexDirection: "column",
            justifyContent: "space-between",
          }}
        >
          <div>
            <div style={{ display: "flex", alignItems: "center", gap: 12, marginBottom: 8 }}>
              <div
                style={{
                  width: 38,
                  height: 38,
                  borderRadius: 10,
                  backgroundColor: THEME.bgSubtle,
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                }}
              >
                <FolderIcon size={20} color={THEME.primary} strokeWidth={2} />
              </div>
              <span style={{ fontSize: 20, fontWeight: 700, color: THEME.textPrimary }}>
                Drag & Drop File Transfer
              </span>
            </div>
            <p style={{ margin: 0, fontSize: 14, color: THEME.textSecondary, lineHeight: 1.5 }}>
              Move files and folders between computers simply by dragging them directly onto the remote desktop screen.
            </p>
          </div>

          <div style={{ marginTop: 14 }}>
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", fontSize: 13, marginBottom: 8 }}>
              <span style={{ fontWeight: 600, color: THEME.textPrimary }}>Project_Backup.zip (148 MB)</span>
              {!isComplete ? (
                <span style={{ color: THEME.primary, fontWeight: 700 }}>{Math.floor(transferProgress)}%</span>
              ) : (
                <div
                  style={{
                    display: "flex",
                    alignItems: "center",
                    gap: 5,
                    backgroundColor: "rgba(16, 185, 129, 0.12)",
                    border: "1px solid rgba(16, 185, 129, 0.3)",
                    padding: "3px 10px",
                    borderRadius: 8,
                    color: THEME.success,
                    fontWeight: 700,
                    fontSize: 12,
                    transform: `scale(${Math.max(0, completeBadgeSpring)})`,
                  }}
                >
                  <CheckIcon size={12} color={THEME.success} strokeWidth={2.5} />
                  <span>100% Uploaded</span>
                </div>
              )}
            </div>

            {/* Progress Track */}
            <div
              style={{
                width: "100%",
                height: 8,
                backgroundColor: THEME.bgSubtle,
                borderRadius: 4,
                overflow: "hidden",
                boxShadow: isComplete ? `0 0 ${successGlow}px rgba(16, 185, 129, 0.5)` : "none",
              }}
            >
              <div
                style={{
                  width: `${transferProgress}%`,
                  height: "100%",
                  backgroundColor: isComplete ? THEME.success : THEME.primary,
                  borderRadius: 4,
                }}
              />
            </div>

            {/* Dynamic Status / Completed Toast */}
            {!isComplete ? (
              <div style={{ display: "flex", justifyContent: "space-between", fontSize: 12, color: THEME.textMuted, marginTop: 8 }}>
                <span>Direct peer transfer</span>
                <span>In progress...</span>
              </div>
            ) : (
              <div
                style={{
                  marginTop: 8,
                  display: "flex",
                  alignItems: "center",
                  gap: 8,
                  padding: "6px 12px",
                  backgroundColor: "rgba(16, 185, 129, 0.08)",
                  border: "1px solid rgba(16, 185, 129, 0.25)",
                  borderRadius: 8,
                  opacity: Math.min(1, Math.max(0, toastSpring)),
                  transform: `translateY(${interpolate(toastSpring, [0, 1], [6, 0], { extrapolateRight: "clamp" })}px)`,
                }}
              >
                <div
                  style={{
                    width: 16,
                    height: 16,
                    borderRadius: "50%",
                    backgroundColor: THEME.success,
                    display: "flex",
                    alignItems: "center",
                    justifyContent: "center",
                    transform: `scale(${Math.min(1.2, Math.max(0, checkmarkSpring))})`,
                  }}
                >
                  <CheckIcon size={10} color="#FFF" strokeWidth={3} />
                </div>
                <span style={{ fontSize: 12, fontWeight: 600, color: THEME.textPrimary }}>
                  Transfer complete • Saved to remote Downloads
                </span>
              </div>
            )}
          </div>
        </div>

        {/* Bento 2: Embedded Remote Terminal */}
        <div
          style={{
            backgroundColor: THEME.bgDarkCard,
            borderRadius: 20,
            padding: "26px",
            border: `1px solid ${THEME.borderDark}`,
            boxShadow: THEME.shadowCard,
            opacity: Math.min(1, Math.max(0, bento2)),
            transform: `scale(${interpolate(bento2, [0, 1], [0.95, 1], { extrapolateRight: "clamp" })})`,
            color: "#FFF",
            display: "flex",
            flexDirection: "column",
            justifyContent: "space-between",
          }}
        >
          <div>
            <div style={{ display: "flex", alignItems: "center", gap: 12, marginBottom: 8 }}>
              <div
                style={{
                  width: 38,
                  height: 38,
                  borderRadius: 10,
                  backgroundColor: "rgba(255, 255, 255, 0.08)",
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                }}
              >
                <TerminalIcon size={20} color="#38EF7D" strokeWidth={2} />
              </div>
              <span style={{ fontSize: 20, fontWeight: 700, color: "#FFF" }}>
                Remote Terminal & Quick Fixes
              </span>
            </div>
            <p style={{ margin: 0, fontSize: 14, color: "#AAA", lineHeight: 1.5 }}>
              Run maintenance commands and troubleshooting tasks in the background without disturbing the user.
            </p>
          </div>

          <div
            style={{
              backgroundColor: "rgba(0,0,0,0.5)",
              padding: "14px 18px",
              borderRadius: 12,
              fontFamily: "monospace",
              fontSize: 13,
              color: "#38EF7D",
              marginTop: 12,
              border: "1px solid rgba(255,255,255,0.08)",
            }}
          >
            <div>&gt; Remote Administration Console</div>
            <div style={{ color: "#EEE", marginTop: 4 }}>Status: Ready • Background execution active</div>
          </div>
        </div>

        {/* Bento 3: Privacy Mode & Remote Reboot */}
        <div
          style={{
            backgroundColor: THEME.bgCard,
            borderRadius: 20,
            padding: "28px",
            border: `1px solid ${THEME.border}`,
            boxShadow: THEME.shadowCard,
            opacity: Math.min(1, Math.max(0, bento3)),
            transform: `scale(${interpolate(bento3, [0, 1], [0.95, 1], { extrapolateRight: "clamp" })})`,
            display: "flex",
            flexDirection: "column",
            justifyContent: "space-between",
          }}
        >
          <div>
            <div style={{ display: "flex", alignItems: "center", gap: 12, marginBottom: 8 }}>
              <div
                style={{
                  width: 38,
                  height: 38,
                  borderRadius: 10,
                  backgroundColor: THEME.bgSubtle,
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                }}
              >
                <ShieldIcon size={20} color={THEME.primary} strokeWidth={2} />
              </div>
              <span style={{ fontSize: 20, fontWeight: 700, color: THEME.textPrimary }}>
                Privacy Curtain & Remote Reboot
              </span>
            </div>
            <p style={{ margin: 0, fontSize: 14, color: THEME.textSecondary, lineHeight: 1.5 }}>
              Black out the physical monitor to protect sensitive work, lock local input, or restart and reconnect seamlessly.
            </p>
          </div>

          <div style={{ display: "flex", gap: 12, marginTop: 14 }}>
            <div style={{ display: "flex", alignItems: "center", gap: 6, fontSize: 13, padding: "8px 14px", backgroundColor: THEME.bgSubtle, borderRadius: 10, fontWeight: 600, color: THEME.textPrimary }}>
              <LockIcon size={14} color={THEME.primary} strokeWidth={2} />
              <span>Screen Curtain</span>
            </div>
            <div style={{ display: "flex", alignItems: "center", gap: 6, fontSize: 13, padding: "8px 14px", backgroundColor: THEME.bgSubtle, borderRadius: 10, fontWeight: 600, color: THEME.textPrimary }}>
              <RefreshIcon size={14} color={THEME.primary} strokeWidth={2} />
              <span>Auto-Reconnect</span>
            </div>
            <div style={{ display: "flex", alignItems: "center", gap: 6, fontSize: 13, padding: "8px 14px", backgroundColor: THEME.bgSubtle, borderRadius: 10, fontWeight: 600, color: THEME.textPrimary }}>
              <KeyboardIcon size={14} color={THEME.primary} strokeWidth={2} />
              <span>Input Lock</span>
            </div>
          </div>
        </div>

        {/* Bento 4: Audio Intercom & Screen Whiteboard */}
        <div
          style={{
            backgroundColor: THEME.bgCard,
            borderRadius: 20,
            padding: "28px",
            border: `1px solid ${THEME.border}`,
            boxShadow: THEME.shadowCard,
            opacity: Math.min(1, Math.max(0, bento4)),
            transform: `scale(${interpolate(bento4, [0, 1], [0.95, 1], { extrapolateRight: "clamp" })})`,
            display: "flex",
            flexDirection: "column",
            justifyContent: "space-between",
          }}
        >
          <div>
            <div style={{ display: "flex", alignItems: "center", gap: 12, marginBottom: 8 }}>
              <div
                style={{
                  width: 38,
                  height: 38,
                  borderRadius: 10,
                  backgroundColor: THEME.bgSubtle,
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                }}
              >
                <MicIcon size={20} color={THEME.primary} strokeWidth={2} />
              </div>
              <span style={{ fontSize: 20, fontWeight: 700, color: THEME.textPrimary }}>
                Voice Chat & Screen Whiteboard
              </span>
            </div>
            <p style={{ margin: 0, fontSize: 14, color: THEME.textSecondary, lineHeight: 1.5 }}>
              Talk in real time with two-way voice chat, listen to system audio, and sketch instructions directly on the screen.
            </p>
          </div>

          <div style={{ display: "flex", gap: 12, marginTop: 14 }}>
            <div style={{ display: "flex", alignItems: "center", gap: 6, fontSize: 13, padding: "8px 14px", backgroundColor: THEME.bgSubtle, borderRadius: 10, fontWeight: 600, color: THEME.textPrimary }}>
              <VolumeIcon size={14} color={THEME.primary} strokeWidth={2} />
              <span>Computer Audio</span>
            </div>
            <div style={{ display: "flex", alignItems: "center", gap: 6, fontSize: 13, padding: "8px 14px", backgroundColor: THEME.bgSubtle, borderRadius: 10, fontWeight: 600, color: THEME.textPrimary }}>
              <PenIcon size={14} color={THEME.primary} strokeWidth={2} />
              <span>Live Annotations</span>
            </div>
            <div style={{ display: "flex", alignItems: "center", gap: 6, fontSize: 13, padding: "8px 14px", backgroundColor: THEME.bgSubtle, borderRadius: 10, fontWeight: 600, color: THEME.textPrimary }}>
              <MicIcon size={14} color={THEME.primary} strokeWidth={2} />
              <span>Voice Chat</span>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};
