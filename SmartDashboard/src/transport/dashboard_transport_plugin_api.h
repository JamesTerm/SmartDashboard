#pragma once

/// @file dashboard_transport_plugin_api.h
/// @brief Versioned C ABI for optional SmartDashboard transport plugins.
///
/// Ian: To add a new transport plugin (e.g. Glass), create a new directory
/// under plugins/<Name>Transport/ with its own CMakeLists.txt, implement
/// the sd_transport_plugin_descriptor_v1 contract, and export
/// SdGetTransportPluginV1().  See NT4Transport/ as the reference
/// example — it covers: NT4 WebSocket client, publish (write-back),
/// chooser support, host-level auto-connect integration, WSAStartup
/// deferred init, and connection field descriptors.
///
/// This header is the binary contract between the SmartDashboard host process
/// and transport plugins loaded from the `plugins/` directory.
///
/// Design goals:
/// - keep the ABI simple and stable across DLL boundaries
/// - avoid exposing raw C++ classes across the plugin boundary
/// - make the interface teachable for students reading a real plugin example
/// - allow host/plugin evolution through explicit versioning
///
/// Compatibility notes:
/// - this is intentionally a C ABI even though the project uses C++ internally
/// - plugins should treat host-provided pointers as borrowed data
/// - the host should treat plugin descriptor pointers as valid only while the
///   plugin library remains loaded
/// - future ABI revisions should add new versioned structs/functions instead of
///   mutating the layout or meaning of existing ones
///
/// Property-query design rules:
/// - use shared host-defined property-name constants for cross-plugin meaning
/// - keep property names stable, lowercase, and `snake_case`
/// - return the supplied default value for unknown properties
/// - property queries should be cheap, side-effect free, and safe to call more
///   than once
/// - keep a few fundamental fixed fields/flags for core host behavior, and use
///   property queries for extensible feature growth

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
    #if defined(SD_TRANSPORT_PLUGIN_EXPORTS)
        #define SD_TRANSPORT_PLUGIN_EXPORT __declspec(dllexport)
    #else
        #define SD_TRANSPORT_PLUGIN_EXPORT __declspec(dllimport)
    #endif
#else
    #define SD_TRANSPORT_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    /// @brief ABI version implemented by this header.
    ///
    /// A plugin reports this value in its descriptor so the host can reject
    /// incompatible binaries up front instead of guessing how to call them.
    SD_TRANSPORT_PLUGIN_API_VERSION_1 = 1u
};

/// @brief Shared property-name constant for chooser/sendable chooser support.
static const char SD_TRANSPORT_PROPERTY_SUPPORTS_CHOOSER[] = "supports_chooser";

/// @brief Shared property-name constant for multi-client support.
static const char SD_TRANSPORT_PROPERTY_SUPPORTS_MULTI_CLIENT[] = "supports_multi_client";

/// @brief Shared settings field id for host or IP address.
static const char SD_TRANSPORT_FIELD_HOST[] = "host";

/// @brief Shared settings field id for team number.
static const char SD_TRANSPORT_FIELD_TEAM_NUMBER[] = "team_number";

/// @brief Shared settings field id for whether team-number resolution is enabled.
static const char SD_TRANSPORT_FIELD_USE_TEAM_NUMBER[] = "use_team_number";

/// @brief Shared settings field id for a client/display name.
static const char SD_TRANSPORT_FIELD_CLIENT_NAME[] = "client_name";

/// @brief Capability flags reported by a plugin descriptor.
///
/// These flags let the host adjust UI and expectations without hardcoding
/// plugin-specific behavior in the main executable.
enum sd_transport_plugin_flags_v1
{
    /// @brief Plugin supports live-session recording.
    ///
    /// When this flag is present, the host may enable recording-oriented UI
    /// and logic for this transport.
    SD_TRANSPORT_PLUGIN_FLAG_SUPPORTS_RECORDING = 1u << 0,

    /// @brief Plugin prefers shortened display labels for keys.
    ///
    /// Example: `Drive/Left/Velocity` may be displayed as the final path
    /// segment in dashboard tile titles. This is only a UI hint.
    SD_TRANSPORT_PLUGIN_FLAG_USE_SHORT_DISPLAY_KEYS = 1u << 1
};

/// @brief Connection states reported from plugin to host.
///
/// These values mirror the host's small connection-state vocabulary so plugins
/// can report status without inventing transport-specific UI enums.
enum sd_transport_connection_state_v1
{
    SD_TRANSPORT_CONNECTION_STATE_DISCONNECTED = 0,
    SD_TRANSPORT_CONNECTION_STATE_CONNECTING = 1,
    SD_TRANSPORT_CONNECTION_STATE_CONNECTED = 2,
    SD_TRANSPORT_CONNECTION_STATE_STALE = 3
};

/// @brief Value tags used by `sd_transport_value_v1`.
///
/// The active payload field is selected by `sd_transport_value_v1::type`.
/// Receivers should ignore unused fields.
enum sd_transport_value_type_v1
{
    SD_TRANSPORT_VALUE_TYPE_BOOL = 1,
    SD_TRANSPORT_VALUE_TYPE_DOUBLE = 2,
    SD_TRANSPORT_VALUE_TYPE_STRING = 3,
    SD_TRANSPORT_VALUE_TYPE_STRING_ARRAY = 4
};

/// @brief Tagged value passed from plugin to host.
///
/// Programming model:
/// - set `type` first
/// - populate the field(s) associated with that type
/// - the host reads only the active field implied by `type`
///
/// Lifetime rules:
/// - `string_value` must remain valid for the duration of the callback
/// - `string_array_items` and each referenced string must remain valid for the
///   duration of the callback
/// - the host copies data it needs; the plugin keeps ownership
struct sd_transport_value_v1
{
    /// @brief Active type tag from `sd_transport_value_type_v1`.
    uint32_t type;

    /// @brief Bool payload when `type == SD_TRANSPORT_VALUE_TYPE_BOOL`.
    ///
    /// Non-zero means true.
    int bool_value;

    /// @brief Double payload when `type == SD_TRANSPORT_VALUE_TYPE_DOUBLE`.
    double double_value;

    /// @brief UTF-8 string payload when `type == SD_TRANSPORT_VALUE_TYPE_STRING`.
    const char* string_value;

    /// @brief UTF-8 string-array payload when `type == SD_TRANSPORT_VALUE_TYPE_STRING_ARRAY`.
    const char* const* string_array_items;

    /// @brief Number of entries in `string_array_items`.
    size_t string_array_count;
};

/// @brief Host-to-plugin connection configuration bundle.
///
/// This struct carries the common values the host knows how to pass across the
/// ABI boundary. A given plugin may ignore most of these fields.
///
/// Rationale:
/// - `transport_id` tells the plugin which registered transport identity the
///   host believes it is starting
/// - `plugin_settings_json` is an escape hatch for plugin-specific settings
/// - NT and replay-related fields exist because the current host already knows
///   how to persist and forward them
///
/// Lifetime rule:
/// - string pointers are borrowed and remain valid only during `start()`
/// - plugins should copy anything they need after `start()` returns
struct sd_transport_connection_config_v1
{
    /// @brief Host-selected transport id such as `legacy-nt`.
    ///
    /// UTF-8 expected.
    const char* transport_id;

    /// @brief Optional plugin-owned settings payload encoded as UTF-8 JSON.
    const char* plugin_settings_json;

    /// @brief Optional NT-style host name or IP address.
    ///
    /// UTF-8 expected.
    const char* nt_host;

    /// @brief Optional NT-style team number.
    ///
    /// Meaning is defined by the plugin.
    int nt_team;

    /// @brief Non-zero when team-number resolution should be preferred.
    int nt_use_team;

    /// @brief Suggested client name for transports that expose one.
    ///
    /// UTF-8 expected.
    const char* nt_client_name;

    /// @brief Optional replay file path.
    ///
    /// UTF-8 expected.
    const char* replay_file_path;
};

/// @brief Opaque plugin transport instance handle.
///
/// The host never inspects this handle. It is created, owned, and interpreted
/// entirely by the plugin implementation.
typedef void* sd_transport_instance_v1;

/// @brief Bool-property query used for extensible transport capabilities.
///
/// @param property_name
/// UTF-8 property name. Host-defined shared constants should be preferred.
/// @param default_value
/// Value the host wants back when the property is unknown.
/// @return Non-zero for true, zero for false.
///
/// @note Unknown properties should return `default_value` unchanged.
typedef int (*sd_transport_get_bool_property_v1_fn)(const char* property_name, int default_value);

/// @brief Supported host-rendered connection field types.
enum sd_transport_connection_field_type_v1
{
    SD_TRANSPORT_CONNECTION_FIELD_TYPE_BOOL = 1,
    SD_TRANSPORT_CONNECTION_FIELD_TYPE_INT = 2,
    SD_TRANSPORT_CONNECTION_FIELD_TYPE_STRING = 3
};

/// @brief Plugin-described host-rendered connection/settings field.
///
/// This lets a plugin describe its connection requirements while keeping UI
/// ownership in the host application.
struct sd_transport_connection_field_descriptor_v1
{
    /// @brief Stable field id used in persisted settings.
    const char* field_id;

    /// @brief Human-readable label shown in the host dialog.
    const char* label;

    /// @brief Field type from `sd_transport_connection_field_type_v1`.
    uint32_t field_type;

    /// @brief Optional help text shown to the user.
    const char* help_text;

    /// @brief Default bool value for bool fields.
    int default_bool_value;

    /// @brief Default int value for int fields.
    int default_int_value;

    /// @brief Default UTF-8 string value for string fields.
    const char* default_string_value;

    /// @brief Minimum value for int fields.
    int int_minimum;

    /// @brief Maximum value for int fields.
    int int_maximum;
};

/// @brief Optional field-schema callback for host-rendered connection settings.
///
/// @param out_count
/// Receives the number of field descriptors returned.
/// @return
/// Pointer to a stable array of field descriptors, or null when the transport
/// has no custom connection fields.
typedef const struct sd_transport_connection_field_descriptor_v1* (*sd_transport_get_connection_fields_v1_fn)(size_t* out_count);

/// @brief Callback used by the plugin to deliver a variable update.
///
/// @param user_data
/// Opaque pointer originally provided by the host in
/// `sd_transport_callbacks_v1::user_data`.
/// @param key
/// UTF-8 key path for the updated variable.
/// @param value
/// Pointer to the tagged value payload.
/// @param seq
/// Plugin-defined monotonic sequence number for reconnect/order handling.
///
/// @note The host copies data it needs during the callback.
typedef void (*sd_transport_on_variable_update_fn_v1)(
    void* user_data,
    const char* key,
    const struct sd_transport_value_v1* value,
    uint64_t seq
);

/// @brief Callback used by the plugin to report connection-state changes.
///
/// @param user_data
/// Opaque pointer originally provided by the host.
/// @param state
/// One of `sd_transport_connection_state_v1`.
typedef void (*sd_transport_on_connection_state_fn_v1)(
    void* user_data,
    int state
);

/// @brief Callback bundle passed from host to plugin during `start()`.
///
/// Plugins may store these function pointers if they need asynchronous delivery
/// after `start()` returns.
struct sd_transport_callbacks_v1
{
    /// @brief Host-owned opaque pointer passed back unchanged on callbacks.
    void* user_data;

    /// @brief Variable-delivery callback.
    ///
    /// Required if the plugin emits live telemetry.
    sd_transport_on_variable_update_fn_v1 on_variable_update;

    /// @brief Optional status callback for connect/disconnect/stale events.
    sd_transport_on_connection_state_fn_v1 on_connection_state;
};

/// @brief Runtime function table implemented by a transport plugin.
///
/// The host never links against a plugin-specific C++ class. It only calls
/// through this function table.
///
/// Return-value convention:
/// - functions returning `int` use non-zero for success and zero for failure
struct sd_transport_api_v1
{
    /// @brief Create a new plugin-owned transport instance.
    /// @return Opaque instance handle on success, or null on failure.
    sd_transport_instance_v1 (*create)(void);

    /// @brief Destroy a transport instance created by `create()`.
    /// @param instance Opaque handle previously returned by `create()`.
    void (*destroy)(sd_transport_instance_v1 instance);

    /// @brief Start the transport instance.
    /// @param instance Plugin-owned transport handle.
    /// @param config Borrowed pointer to host connection/configuration data.
    /// @param callbacks Borrowed pointer to host callback functions.
    /// @return Non-zero on success, zero on failure.
    /// @note If the plugin needs values after `start()` returns, it should copy them.
    int (*start)(
        sd_transport_instance_v1 instance,
        const struct sd_transport_connection_config_v1* config,
        const struct sd_transport_callbacks_v1* callbacks
    );

    /// @brief Stop the transport instance.
    ///
    /// Implementations should stop background work, disconnect resources, and
    /// stop issuing callbacks for the stopped session.
    ///
    /// @param instance Plugin-owned transport handle.
    void (*stop)(sd_transport_instance_v1 instance);

    /// @brief Publish a bool value from host to plugin transport.
    /// @param instance Plugin-owned transport handle.
    /// @param key UTF-8 key path to publish.
    /// @param value Bool payload encoded as zero/non-zero.
    /// @return Non-zero on success, zero on failure.
    int (*publish_bool)(sd_transport_instance_v1 instance, const char* key, int value);

    /// @brief Publish a double value from host to plugin transport.
    /// @param instance Plugin-owned transport handle.
    /// @param key UTF-8 key path to publish.
    /// @param value Double payload to publish.
    /// @return Non-zero on success, zero on failure.
    int (*publish_double)(sd_transport_instance_v1 instance, const char* key, double value);

    /// @brief Publish a UTF-8 string value from host to plugin transport.
    /// @param instance Plugin-owned transport handle.
    /// @param key UTF-8 key path to publish.
    /// @param value UTF-8 string payload to publish.
    /// @return Non-zero on success, zero on failure.
    int (*publish_string)(sd_transport_instance_v1 instance, const char* key, const char* value);
};

/// @brief Static plugin descriptor returned by the exported entry point.
///
/// This descriptor tells the host:
/// - which ABI version the plugin implements
/// - what the plugin should be called in the UI
/// - which capability flags it advertises
/// - how the host can query extensible boolean properties
/// - which function table should be used for runtime operations
///
/// Lifetime rule:
/// - plugins normally back this with static storage
/// - it must remain valid while the DLL is loaded
struct sd_transport_plugin_descriptor_v1
{
    /// @brief Size of this struct as compiled by the plugin.
    ///
    /// Useful for future compatibility checks.
    uint32_t struct_size;

    /// @brief ABI version implemented by this descriptor.
    uint32_t plugin_api_version;

    /// @brief Stable transport id used for selection/settings.
    ///
    /// Example: `legacy-nt`.
    const char* plugin_id;

    /// @brief Human-friendly display name shown in menus.
    ///
    /// Example: `Legacy NT`.
    const char* display_name;

    /// @brief Host-facing settings profile id.
    ///
    /// Plugins that share a settings shape could theoretically reuse a profile,
    /// but the normal case is one profile id per plugin/ecosystem.
    const char* settings_profile_id;

    /// @brief Capability flags from `sd_transport_plugin_flags_v1`.
    uint32_t flags;

    /// @brief Optional extensible bool-property query callback.
    ///
    /// This lets the host ask about feature support without consuming more fixed
    /// flag bits for every future capability. A null pointer means "use host
    /// defaults for all bool properties".
    sd_transport_get_bool_property_v1_fn get_bool_property;

    /// @brief Optional host-rendered connection field schema callback.
    sd_transport_get_connection_fields_v1_fn get_connection_fields;

    /// @brief Pointer to the plugin's runtime function table.
    const struct sd_transport_api_v1* transport_api;
};

/// @brief Function-pointer type for dynamically resolving the export.
typedef const struct sd_transport_plugin_descriptor_v1* (*sd_get_transport_plugin_v1_fn)(void);

/// @brief Exported plugin discovery function.
///
/// The host loads the plugin DLL, resolves this symbol by name, and calls it to
/// obtain the plugin descriptor for ABI version 1.
///
/// @return Pointer to a static `sd_transport_plugin_descriptor_v1`, or null if
/// the plugin cannot provide one.
SD_TRANSPORT_PLUGIN_EXPORT const struct sd_transport_plugin_descriptor_v1* SdGetTransportPluginV1(void);

#ifdef __cplusplus
}
#endif
