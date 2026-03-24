#pragma once

/// @file dashboard_transport.h
/// @brief Host-side C++ transport abstractions used by SmartDashboard.
///
/// This header defines the C++ side of SmartDashboard's transport layer. The UI
/// and model code talk to transports through these types instead of depending on
/// a specific protocol implementation.
///
/// This file covers two related concerns:
/// - the runtime transport interface (`IDashboardTransport`)
/// - the discovery/selection layer (`DashboardTransportRegistry`)
///
/// The plugin-facing C ABI lives in `dashboard_transport_plugin_api.h`. This
/// file is the host-side wrapper and application contract used by the rest of
/// the dashboard.

#include <QString>
#include <QVariant>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <memory>
#include <vector>

namespace sd::transport
{
    inline constexpr const char* kTransportPropertySupportsChooser = "supports_chooser";
    inline constexpr const char* kTransportPropertySupportsMultiClient = "supports_multi_client";
    inline constexpr const char* kTransportFieldHost = "host";
    inline constexpr const char* kTransportFieldTeamNumber = "team_number";
    inline constexpr const char* kTransportFieldUseTeamNumber = "use_team_number";
    inline constexpr const char* kTransportFieldClientName = "client_name";

    /// @brief Shared transport property names used by host-side code.
    ///
    /// These constants mirror the plugin ABI property names so the rest of the
    /// dashboard can ask transport descriptors about optional capabilities
    /// without scattering raw string literals throughout the UI.

    /// @brief Supported field types for host-rendered transport settings.
    ///
    /// The host intentionally owns settings UI rendering so plugins do not need
    /// to pass Qt widgets or C++ objects across the ABI boundary.
    enum class ConnectionFieldType
    {
        Bool,
        Int,
        String
    };

    /// @brief Host-rendered connection/settings field descriptor.
    ///
    /// These fields describe transport-specific connection inputs such as host,
    /// team number, serial port, or client name.
    struct ConnectionFieldDescriptor
    {
        /// @brief Stable field id used in persisted settings JSON.
        QString id;

        /// @brief Human-readable label shown in the settings dialog.
        QString label;

        /// @brief Supported field type.
        ConnectionFieldType type = ConnectionFieldType::String;

        /// @brief Optional help text shown to explain the field.
        QString helpText;

        /// @brief Default value used when no persisted value exists.
        QVariant defaultValue;

        /// @brief Lower bound for integer fields.
        int intMinimum = 0;

        /// @brief Upper bound for integer fields.
        int intMaximum = 99999;
    };

    /// @brief Broad transport categories known to the dashboard host.
    ///
    /// `TransportKind` is intentionally coarse. The stable user-selectable
    /// identity is `ConnectionConfig::transportId`, not this enum value. Several
    /// concrete transport ids may share the same kind.
    enum class TransportKind
    {
        /// @brief Built-in native/local transport implementation.
        Direct,

        /// @brief Optional transport loaded from a plugin DLL.
        Plugin,

        /// @brief Built-in replay transport backed by recorded sessions.
        Replay
    };

    /// @brief Connection-state vocabulary used by the dashboard host.
    ///
    /// These values are shared across built-in transports and plugin-backed
    /// transports so the rest of the app can react uniformly.
    enum class ConnectionState
    {
        Disconnected,
        Connecting,
        Connected,
        Stale
    };

    /// @brief User/session-selected transport configuration.
    ///
    /// This struct is the host's persisted configuration model for transport
    /// startup. Some fields are transport-specific and may be ignored by a given
    /// implementation.
    ///
    /// Key design point:
    /// - `transportId` is the stable selection key
    /// - `kind` is a broader category usually derived from the selected
    ///   descriptor
    struct ConnectionConfig
    {
        /// @brief Broad transport category.
        ///
        /// Usually derived from the selected descriptor rather than chosen
        /// independently.
        TransportKind kind = TransportKind::Direct;

        /// @brief Stable selected transport id.
        ///
        /// Examples: `direct`, `replay`, `legacy-nt`.
        QString transportId = "direct";

        /// @brief Optional NT-style host/IP setting for transports that use it.
        QString ntHost = "127.0.0.1";

        /// @brief Optional NT-style team number for transports that support it.
        int ntTeam = 0;

        /// @brief True when NT-style team-number resolution should be preferred.
        bool ntUseTeam = true;

        /// @brief Optional client/display name for transports that expose one.
        QString ntClientName = "SmartDashboardApp";

        /// @brief Plugin-owned JSON settings payload for ecosystem-specific options.
        QString pluginSettingsJson;

        /// @brief Replay session file path used by the built-in replay transport.
        QString replayFilePath;
    };

    /// @brief Metadata describing a transport available to the host.
    ///
    /// A descriptor answers questions such as:
    /// - what transport ids exist right now?
    /// - what should the Connection menu call them?
    /// - does the transport support recording or playback?
    /// - should keys be displayed in shortened form?
    /// - what optional extensible properties does the transport advertise?
    ///
    /// Property-query design rules:
    /// - use shared host-defined property-name constants when a property needs
    ///   to influence common UI or workflow logic
    /// - treat unknown properties as "use the supplied default"
    /// - keep properties additive and side-effect free
    /// - do not move core transport identity into ad hoc properties when a
    ///   normal field already expresses it clearly
    struct TransportDescriptor
    {
        /// @brief Stable id used for selection and settings persistence.
        QString id;

        /// @brief Human-readable display name for menus and status text.
        QString displayName;

        /// @brief Broad transport category.
        TransportKind kind = TransportKind::Direct;

        /// @brief UI hint indicating that tile titles may use short key labels.
        bool useShortDisplayKeys = false;

        /// @brief Capability hint indicating that live recording is supported.
        bool supportsRecording = false;

        /// @brief Capability hint indicating that playback controls are meaningful.
        bool supportsPlayback = false;

        /// @brief Settings-profile identifier used to gate settings/UI behavior.
        QString settingsProfileId;

        /// @brief Host-rendered connection/settings fields for this transport.
        std::vector<ConnectionFieldDescriptor> connectionFields;

        /// @brief Extensible boolean property overrides keyed by shared string id.
        std::map<QString, bool> boolProperties;

        /// @brief Query an extensible bool property with a default fallback.
        /// @param propertyName Stable property key such as `supports_chooser`.
        /// @param defaultValue Value to use when the property is not present.
        /// @return Property value if present, otherwise `defaultValue`.
        bool GetBoolProperty(const QString& propertyName, bool defaultValue = false) const
        {
            const auto it = boolProperties.find(propertyName);
            if (it == boolProperties.end())
            {
                return defaultValue;
            }

            return it->second;
        }

        /// @brief Return true when the transport exposes configurable settings fields.
        bool HasConnectionFields() const
        {
            return !connectionFields.empty();
        }
    };

    /// @brief Normalized variable update delivered to the dashboard.
    ///
    /// Each transport converts its native payloads into this common form before
    /// the rest of the application sees them.
    struct VariableUpdate
    {
        /// @brief Variable key/path such as `Drive/Speed`.
        QString key;

        /// @brief Normalized transport value-type id understood by the dashboard.
        int valueType = 0;

        /// @brief Current value payload stored as a Qt variant for UI/model use.
        QVariant value;

        /// @brief Monotonic sequence number when the source transport can supply one.
        std::uint64_t seq = 0;
    };

    /// @brief Marker categories used by replay and timeline UI.
    ///
    /// These categories are intentionally small and UI-focused. They let
    /// transports provide timeline annotations without coupling the UI to
    /// transport-specific event schemas.
    enum class PlaybackMarkerKind
    {
        Connect,
        Disconnect,
        Stale,
        Anomaly,
        Generic
    };

    /// @brief Timeline marker exposed by playback-capable transports.
    struct PlaybackMarker
    {
        /// @brief Marker timestamp in microseconds from the session/replay origin.
        std::int64_t timestampUs = 0;

        /// @brief Marker category used for color/icon/rendering decisions.
        PlaybackMarkerKind kind = PlaybackMarkerKind::Generic;

        /// @brief Human-readable label shown in marker lists and tooltips.
        QString label;
    };

    /// @brief Callback invoked when a transport emits a normalized variable update.
    using VariableUpdateCallback = std::function<void(const VariableUpdate&)>;

    /// @brief Callback invoked when a transport changes connection state.
    using ConnectionStateCallback = std::function<void(ConnectionState)>;

    /// @brief Runtime transport interface used by the dashboard host.
    ///
    /// Implementations may be built-in (`Direct`, `Replay`) or backed by a
    /// plugin. The rest of the dashboard should depend on this interface rather
    /// than on protocol-specific implementations.
    class IDashboardTransport
    {
    public:
        virtual ~IDashboardTransport() = default;

        /// @brief Start the transport and provide callbacks for future updates.
        /// @param onVariableUpdate Called when normalized variable updates arrive.
        /// @param onConnectionState Called when connection state changes.
        /// @return True on successful start, false on failure.
        virtual bool Start(VariableUpdateCallback onVariableUpdate, ConnectionStateCallback onConnectionState) = 0;

        /// @brief Stop the transport and release any active session resources.
        ///
        /// Implementations should stop background work and prevent further
        /// callback delivery for the stopped session.
        virtual void Stop() = 0;

        /// @brief Publish a bool command/value through the active transport.
        /// @param key Variable/control key to publish.
        /// @param value Bool payload to publish.
        /// @return True on success, false on failure.
        virtual bool PublishBool(const QString& key, bool value) = 0;

        /// @brief Publish a double command/value through the active transport.
        /// @param key Variable/control key to publish.
        /// @param value Double payload to publish.
        /// @return True on success, false on failure.
        virtual bool PublishDouble(const QString& key, double value) = 0;

        /// @brief Publish a string command/value through the active transport.
        /// @param key Variable/control key to publish.
        /// @param value String payload to publish.
        /// @return True on success, false on failure.
        virtual bool PublishString(const QString& key, const QString& value) = 0;

        /// @brief Replay retained dashboard-owned controls into the UI model.
        /// @param replayFn Callback invoked for each retained control value.
        ///
        /// This is primarily useful for transports that can remember latest
        /// operator-controlled values across reconnects or process restarts.
        ///
        /// Default behavior: no retained replay support.
        virtual void ReplayRetainedControls(const std::function<void(const QString& key, int valueType, const QVariant& value)>& replayFn)
        {
            static_cast<void>(replayFn);
        }

        /// @brief Return true when this transport exposes playback semantics.
        virtual bool SupportsPlayback() const
        {
            return false;
        }

        /// @brief Set play/pause state for playback-capable transports.
        /// @param isPlaying True to play, false to pause.
        /// @return True on success, false if unsupported or failed.
        virtual bool SetPlaybackPlaying(bool isPlaying)
        {
            static_cast<void>(isPlaying);
            return false;
        }

        /// @brief Seek the playback cursor for playback-capable transports.
        /// @param cursorUs Target playback position in microseconds.
        /// @return True on success, false if unsupported or failed.
        virtual bool SeekPlaybackUs(std::int64_t cursorUs)
        {
            static_cast<void>(cursorUs);
            return false;
        }

        /// @brief Set playback speed multiplier for playback-capable transports.
        /// @param rate Playback multiplier such as 0.5, 1.0, or 2.0.
        /// @return True on success, false if unsupported or failed.
        virtual bool SetPlaybackRate(double rate)
        {
            static_cast<void>(rate);
            return false;
        }

        /// @brief Return total playback duration in microseconds.
        virtual std::int64_t GetPlaybackDurationUs() const
        {
            return 0;
        }

        /// @brief Return current playback cursor in microseconds.
        virtual std::int64_t GetPlaybackCursorUs() const
        {
            return 0;
        }

        /// @brief Return true if playback is currently running.
        virtual bool IsPlaybackPlaying() const
        {
            return false;
        }

        /// @brief Return timeline markers provided by the transport.
        virtual std::vector<PlaybackMarker> GetPlaybackMarkers() const
        {
            return {};
        }
    };

    /// @brief Registry exposing built-in transports plus any discovered plugins.
    ///
    /// Typical usage:
    /// - construct at app startup
    /// - ask for available descriptors to populate the Connection menu
    /// - persist the selected `transportId`
    /// - call `CreateTransport()` with the chosen `ConnectionConfig`
    class DashboardTransportRegistry
    {
    public:
        /// @brief Create the registry and discover built-in/plugin transports.
        DashboardTransportRegistry();

        /// @brief Destroy the registry and unload plugin bookkeeping/resources.
        ~DashboardTransportRegistry();

        DashboardTransportRegistry(const DashboardTransportRegistry&) = delete;
        DashboardTransportRegistry& operator=(const DashboardTransportRegistry&) = delete;

        /// @brief Return all currently available transport descriptors.
        /// @return Stable reference valid for the lifetime of the registry.
        const std::vector<TransportDescriptor>& GetAvailableTransports() const;

        /// @brief Look up a descriptor by stable transport id.
        /// @param transportId Stable id such as `direct`, `replay`, or `legacy-nt`.
        /// @return Pointer to the descriptor if found, otherwise null.
        const TransportDescriptor* FindTransport(const QString& transportId) const;

        /// @brief Create a transport instance matching the supplied configuration.
        /// @param config Host-side transport configuration.
        /// @return New transport instance on success, or null on failure.
        std::unique_ptr<IDashboardTransport> CreateTransport(const ConnectionConfig& config) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

    /// @brief Convert a broad transport kind to a short display string.
    /// @param kind Transport category to convert.
    /// @return Short human-readable string such as `Direct`, `Plugin`, or `Replay`.
    QString ToDisplayString(TransportKind kind);
}
