import React from "react";
import {
  interpolate,
  spring,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
  Img,
} from "remotion";
import { THEME } from "../theme";
import {
  LaptopIcon,
  MonitorIcon,
  FolderIcon,
  MicIcon,
  CheckIcon,
  LockIcon,
  DownloadIcon,
} from "../Icons";

export const Scene5Outro: React.FC = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();

  // Part 1: Interactive Permission & Security card reveal (frames 0 - 170)
  const permCardIn = spring({
    frame: frame - 10,
    fps,
    config: { damping: 14, mass: 0.8, stiffness: 100 },
  });

  const acceptButtonPulse = spring({
    frame: frame - 60,
    fps,
    config: { damping: 12, mass: 0.6, stiffness: 140 },
  });

  // Crossfade between Part 1 and Part 2 without layout displacement
  const part1Opacity = interpolate(frame, [140, 170], [1, 0], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
  });

  const part2Opacity = interpolate(frame, [155, 185], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
  });

  const ctaScale = spring({
    frame: frame - 160,
    fps,
    config: { damping: 14, mass: 0.8, stiffness: 90 },
  });

  const fadeOut = interpolate(frame, [380, 420], [1, 0], {
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
        alignItems: "center",
        justifyContent: "center",
        fontFamily: "'Segoe UI', -apple-system, BlinkMacSystemFont, sans-serif",
        position: "relative",
        overflow: "hidden",
        opacity: fadeOut,
      }}
    >
      {/* Background warm glowing orb */}
      <div
        style={{
          position: "absolute",
          top: "20%",
          left: "30%",
          width: 900,
          height: 900,
          borderRadius: "50%",
          background: `radial-gradient(circle, ${THEME.primaryGlow} 0%, rgba(250, 248, 245, 0) 70%)`,
          filter: "blur(70px)",
        }}
      />

      {/* Part 1: Interactive Permission & Session Security Demo */}
      {frame < 180 && (
        <div
          style={{
            position: "absolute",
            display: "flex",
            flexDirection: "column",
            alignItems: "center",
            justifyContent: "center",
            opacity: Math.min(1, Math.max(0, permCardIn)) * part1Opacity,
            transform: `translateY(${interpolate(permCardIn, [0, 1], [30, 0], { extrapolateRight: "clamp" })}px)`,
            zIndex: 10,
          }}
        >
          <span
            style={{
              fontSize: 14,
              fontWeight: 700,
              letterSpacing: "0.12em",
              color: THEME.primary,
              textTransform: "uppercase",
              marginBottom: 10,
            }}
          >
            Session Security & Privacy
          </span>
          <h2
            style={{
              margin: "0 0 32px 0",
              fontSize: 50,
              fontWeight: 800,
              letterSpacing: "-0.03em",
              color: THEME.textPrimary,
              textAlign: "center",
            }}
          >
            You Are Always in Complete Control.
          </h2>

          <div
            style={{
              width: 780,
              backgroundColor: THEME.bgCard,
              borderRadius: 24,
              padding: "34px 40px",
              border: `1px solid ${THEME.border}`,
              boxShadow: THEME.shadowCard,
            }}
          >
            {/* Header row with incoming status */}
            <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 20 }}>
              <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
                <div style={{ width: 10, height: 10, borderRadius: "50%", backgroundColor: THEME.success, boxShadow: `0 0 10px ${THEME.success}` }} />
                <span style={{ fontSize: 13, fontWeight: 700, letterSpacing: "0.08em", color: THEME.textMuted }}>
                  INCOMING SESSION REQUEST
                </span>
              </div>
              <span style={{ fontSize: 13, fontFamily: "monospace", fontWeight: 700, color: THEME.primary, backgroundColor: THEME.bgSubtle, padding: "4px 10px", borderRadius: 8, border: `1px solid ${THEME.border}` }}>
                ID: 482 910 375
              </span>
            </div>

            {/* Requester device info */}
            <div style={{ display: "flex", alignItems: "center", gap: 14, padding: "14px 18px", backgroundColor: THEME.bgSubtle, borderRadius: 14, marginBottom: 20 }}>
              <div
                style={{
                  width: 40,
                  height: 40,
                  borderRadius: 10,
                  backgroundColor: THEME.bgCard,
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                  border: `1px solid ${THEME.border}`,
                }}
              >
                <LaptopIcon size={20} color={THEME.primary} strokeWidth={2} />
              </div>
              <div>
                <div style={{ fontSize: 16, fontWeight: 700, color: THEME.textPrimary }}>Laptop-Studio</div>
                <div style={{ fontSize: 13, color: THEME.textSecondary }}>Requesting remote desktop interaction</div>
              </div>
            </div>

            {/* Granular Permission Toggles */}
            <div style={{ display: "flex", flexDirection: "column", gap: 10, marginBottom: 24 }}>
              {[
                { Icon: MonitorIcon, label: "Remote Screen & Pointer Control", status: "Allowed" },
                { Icon: FolderIcon, label: "File Transfer & Clipboard Sharing", status: "Allowed" },
                { Icon: MicIcon, label: "Two-Way Voice & Audio Streaming", status: "Allowed" },
              ].map((perm, idx) => (
                <div
                  key={idx}
                  style={{
                    display: "flex",
                    justifyContent: "space-between",
                    alignItems: "center",
                    padding: "10px 14px",
                    borderRadius: 10,
                    backgroundColor: THEME.bgCardAlt,
                    border: `1px solid ${THEME.border}`,
                  }}
                >
                  <div style={{ display: "flex", alignItems: "center", gap: 10 }}>
                    <div
                      style={{
                        width: 28,
                        height: 28,
                        borderRadius: 6,
                        backgroundColor: THEME.bgSubtle,
                        display: "flex",
                        alignItems: "center",
                        justifyContent: "center",
                      }}
                    >
                      <perm.Icon size={15} color={THEME.primary} strokeWidth={2} />
                    </div>
                    <span style={{ fontSize: 14, fontWeight: 600, color: THEME.textPrimary }}>{perm.label}</span>
                  </div>
                  <div style={{ display: "flex", alignItems: "center", gap: 5, fontSize: 12, fontWeight: 700, color: THEME.success, backgroundColor: "rgba(16, 185, 129, 0.1)", padding: "3px 8px", borderRadius: 6 }}>
                    <CheckIcon size={12} color={THEME.success} strokeWidth={2.5} />
                    <span>{perm.status}</span>
                  </div>
                </div>
              ))}
            </div>

            {/* Action buttons */}
            <div style={{ display: "flex", gap: 14 }}>
              <div
                style={{
                  flex: 1,
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                  gap: 8,
                  padding: "14px",
                  backgroundColor: THEME.success,
                  borderRadius: 12,
                  color: "#FFF",
                  fontWeight: 700,
                  fontSize: 16,
                  boxShadow: `0 6px 18px rgba(16, 185, 129, 0.35)`,
                  transform: `scale(${interpolate(acceptButtonPulse, [0, 1], [0.98, 1])})`,
                }}
              >
                <CheckIcon size={18} color="#FFF" strokeWidth={2.5} />
                <span>Accept Session</span>
              </div>
              <div
                style={{
                  padding: "14px 28px",
                  backgroundColor: THEME.bgCard,
                  border: `1px solid ${THEME.border}`,
                  borderRadius: 12,
                  color: THEME.textSecondary,
                  fontWeight: 600,
                  fontSize: 16,
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                }}
              >
                Decline
              </div>
            </div>

            {/* Footnote */}
            <div
              style={{
                marginTop: 18,
                display: "flex",
                alignItems: "center",
                justifyContent: "center",
                gap: 8,
                fontSize: 13,
                color: THEME.textMuted,
              }}
            >
              <LockIcon size={13} color={THEME.textMuted} strokeWidth={2} />
              <span>Direct encrypted connection. Revoke permissions or disconnect anytime with 1 click.</span>
            </div>
          </div>
        </div>
      )}

      {/* Part 2: Final Call to Action */}
      {frame >= 150 && (
        <div
          style={{
            position: "absolute",
            display: "flex",
            flexDirection: "column",
            alignItems: "center",
            justifyContent: "center",
            opacity: part2Opacity,
            transform: `scale(${interpolate(ctaScale, [0, 1], [0.94, 1], { extrapolateRight: "clamp" })})`,
            zIndex: 10,
          }}
        >
          {/* App Icon */}
          <div
            style={{
              width: 120,
              height: 120,
              borderRadius: 32,
              backgroundColor: THEME.bgCard,
              border: `1px solid ${THEME.borderAlt}`,
              boxShadow: THEME.shadowFloat,
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
              marginBottom: 24,
            }}
          >
            <Img
              src={staticFile("icon.png")}
              style={{
                width: 92,
                height: 92,
                objectFit: "contain",
              }}
            />
          </div>

          <h2
            style={{
              margin: 0,
              fontSize: 64,
              fontWeight: 800,
              letterSpacing: "-0.03em",
              color: THEME.textPrimary,
            }}
          >
            Get CppDesk Today
          </h2>

          <p
            style={{
              margin: "12px 0 32px 0",
              fontSize: 22,
              color: THEME.textSecondary,
              textAlign: "center",
            }}
          >
            Single portable file. Zero installation. Fast, secure Windows control.
          </p>

          {/* Download CTA Button */}
          <div
            style={{
              display: "flex",
              alignItems: "center",
              gap: 12,
              padding: "18px 44px",
              backgroundColor: THEME.primary,
              borderRadius: 16,
              color: "#FFF",
              fontSize: 22,
              fontWeight: 700,
              boxShadow: `0 12px 32px ${THEME.primaryGlow}`,
              marginBottom: 36,
            }}
          >
            <DownloadIcon size={22} color="#FFF" strokeWidth={2.5} />
            <span>Download CppDesk</span>
          </div>

          {/* Badges Bar */}
          <div style={{ display: "flex", gap: 16 }}>
            {["Windows 10 / 11", "Free & Open Source", "No Setup Required"].map(
              (badge, idx) => (
                <div
                  key={idx}
                  style={{
                    padding: "10px 20px",
                    backgroundColor: THEME.bgCard,
                    borderRadius: 12,
                    border: `1px solid ${THEME.border}`,
                    fontSize: 14,
                    fontWeight: 600,
                    color: THEME.textSecondary,
                  }}
                >
                  {badge}
                </div>
              )
            )}
          </div>
        </div>
      )}
    </div>
  );
};
