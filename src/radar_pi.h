/******************************************************************************
 *
 * Project:  OpenCPN
 * Purpose:  Radar Plugin
 * Author:   David Register
 *           Dave Cowell
 *           Kees Verruijt
 *           Douwe Fokkema
 *           Sean D'Epagnier
 ***************************************************************************
 *   Copyright (C) 2010 by David S. Register              bdbcat@yahoo.com *
 *   Copyright (C) 2012-2013 by Dave Cowell                                *
 *   Copyright (C) 2012-2016 by Kees Verruijt         canboat@verruijt.net *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************
 */

#ifndef _RADARPI_H_
#define _RADARPI_H_

#define MY_API_VERSION_MAJOR 1
#define MY_API_VERSION_MINOR 14  // Needed for PluginAISDrawGL().

#include <algorithm>
#include <vector>
#include "drawutil.h"
#include "jsonreader.h"
#include "nmea0183/nmea0183.h"
#include "pi_common.h"
#include "radar_control_item.h"
#include "socketutil.h"
#include "version.h"

// Load the ocpn_plugin. On OS X this generates many warnings, suppress these.
#ifdef __WXOSX__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverloaded-virtual"
#endif
#include "ocpn_plugin.h"
#ifdef __WXOSX__
#pragma clang diagnostic pop
#endif

PLUGIN_BEGIN_NAMESPACE

// Define the following to make sure we have no race conditions during thread stop.
// #define TEST_THREAD_RACES

//    Forward definitions
class GuardZone;
class RadarInfo;

class ControlsDialog;
class MessageBox;
class OptionsDialog;
class RadarReceive;
class RadarControl;
class radar_pi;
class GuardZoneBogey;
class RadarArpa;

#define RADARS (4)         // Arbitrary limit, anyone running this many is already crazy!
#define GUARD_ZONES (2)    // Could be increased if wanted
#define BEARING_LINES (2)  // And these as well

static const int SECONDS_PER_TIMED_IDLE_SETTING = 5 * 60;  // 5 minutes increment for each setting
static const int SECONDS_PER_TIMED_RUN_SETTING = 10;

#define OPENGL_ROTATION (-90.0)  // Difference between 'up' and OpenGL 'up'...

typedef int SpokeBearing;  // A value from 0 -- LINES_PER_ROTATION indicating a bearing (? = North,
                           // +ve = clockwise)

typedef int AngleDegrees;  // An angle relative to North or HeadUp. Generally [0..359> or [-180,180]

// Use the above to convert from 'raw' headings sent by the radar (0..4095) into classical degrees
// (0..359) and back

// OLD NAVICO
//#define SCALE_RAW_TO_DEGREES2048(raw) ((raw) * (double)DEGREES_PER_ROTATION / LINES_PER_ROTATION)
//#define SCALE_DEGREES_TO_RAW2048(angle) ((int)((angle) * (double)LINES_PER_ROTATION / DEGREES_PER_ROTATION))
//#define MOD_ROTATION(raw) (((raw) + 2 * SPOKES) % SPOKES)

// NEW GENERIC
#define SCALE_DEGREES_TO_SPOKES(angle) ((angle) * (m_ri->m_spokes) / DEGREES_PER_ROTATION)
#define SCALE_SPOKES_TO_DEGREES(raw) ((raw) * (double)DEGREES_PER_ROTATION / m_ri->m_spokes)
#define MOD_SPOKES(raw) (((raw) + 2 * m_ri->m_spokes) % m_ri->m_spokes)
#define MOD_DEGREES(angle) ((angle + 2 * DEGREES_PER_ROTATION) % DEGREES_PER_ROTATION)
#define MOD_DEGREES_FLOAT(angle) (fmod((double)angle + 2 * DEGREES_PER_ROTATION, DEGREES_PER_ROTATION))

#define WATCHDOG_TIMEOUT (10)  // After 10s assume GPS and heading data is invalid

#define TIMED_OUT(t, timeout) (t >= timeout)
#define NOT_TIMED_OUT(t, timeout) (!TIMED_OUT(t, timeout))

#define VALID_GEO(x) (!isnan(x) && x >= -360.0 && x <= +360.0)

#ifndef M_SETTINGS
#define M_SETTINGS m_pi->m_settings
#define M_PLUGIN m_pi->
#else
#define M_PLUGIN
#endif

#define LOGLEVEL_INFO 0
#define LOGLEVEL_VERBOSE 1
#define LOGLEVEL_DIALOG 2
#define LOGLEVEL_TRANSMIT 4
#define LOGLEVEL_RECEIVE 8
#define LOGLEVEL_GUARD 16
#define LOGLEVEL_ARPA 32
#define IF_LOG_AT_LEVEL(x) if ((M_SETTINGS.verbose & x) != 0)
#define IF_LOG_AT(x, y)       \
  do {                        \
    IF_LOG_AT_LEVEL(x) { y; } \
  } while (0)
#define LOG_INFO wxLogMessage
#define LOG_VERBOSE IF_LOG_AT_LEVEL(LOGLEVEL_VERBOSE) wxLogMessage
#define LOG_DIALOG IF_LOG_AT_LEVEL(LOGLEVEL_DIALOG) wxLogMessage
#define LOG_TRANSMIT IF_LOG_AT_LEVEL(LOGLEVEL_TRANSMIT) wxLogMessage
#define LOG_RECEIVE IF_LOG_AT_LEVEL(LOGLEVEL_RECEIVE) wxLogMessage
#define LOG_GUARD IF_LOG_AT_LEVEL(LOGLEVEL_GUARD) wxLogMessage
#define LOG_ARPA IF_LOG_AT_LEVEL(LOGLEVEL_ARPA) wxLogMessage

#define LOG_BINARY_VERBOSE(what, data, size) \
  IF_LOG_AT_LEVEL(LOGLEVEL_VERBOSE) { M_PLUGIN logBinaryData(what, data, size); }
#define LOG_BINARY_DIALOG(what, data, size) \
  IF_LOG_AT_LEVEL(LOGLEVEL_DIALOG) { M_PLUGIN logBinaryData(what, data, size); }
#define LOG_BINARY_TRANSMIT(what, data, size) \
  IF_LOG_AT_LEVEL(LOGLEVEL_TRANSMIT) { M_PLUGIN logBinaryData(what, data, size); }
#define LOG_BINARY_RECEIVE(what, data, size) \
  IF_LOG_AT_LEVEL(LOGLEVEL_RECEIVE) { M_PLUGIN logBinaryData(what, data, size); }
#define LOG_BINARY_GUARD(what, data, size) \
  IF_LOG_AT_LEVEL(LOGLEVEL_GUARD) { M_PLUGIN logBinaryData(what, data, size); }
#define LOG_BINARY_ARPA(what, data, size) \
  IF_LOG_AT_LEVEL(LOGLEVEL_ARPA) { M_PLUGIN logBinaryData(what, data, size); }

enum { BM_ID_RED, BM_ID_RED_SLAVE, BM_ID_GREEN, BM_ID_GREEN_SLAVE, BM_ID_AMBER, BM_ID_AMBER_SLAVE, BM_ID_BLANK, BM_ID_BLANK_SLAVE };

// Arranged from low to high priority:
enum HeadingSource {
  HEADING_NONE,
  HEADING_FIX_COG,
  HEADING_FIX_HDM,
  HEADING_FIX_HDT,
  HEADING_NMEA_HDM,
  HEADING_NMEA_HDT,
  HEADING_RADAR_HDM,
  HEADING_RADAR_HDT
};

enum RadarState { RADAR_OFF, RADAR_WARMING_UP, RADAR_STANDBY, RADAR_TRANSMIT, RADAR_SPINNING_UP };

struct receive_statistics {
  int packets;
  int broken_packets;
  int spokes;
  int broken_spokes;
  int missing_spokes;
};

typedef enum GuardZoneType { GZ_ARC, GZ_CIRCLE } GuardZoneType;

typedef enum RadarType {
#define DEFINE_RADAR(t, n, s, l, a, b, c) t,
#include "RadarType.h"
  RT_MAX
} RadarType;

const size_t RadarSpokes[RT_MAX] = {
#define DEFINE_RADAR(t, n, s, l, a, b, c) s,
#include "RadarType.h"
};

const size_t RadarSpokeLenMax[RT_MAX] = {
#define DEFINE_RADAR(t, n, s, l, a, b, c) l,
#include "RadarType.h"
};

extern const wchar_t *RadarTypeName[RT_MAX];

typedef enum ControlType {
  CT_NONE,
#define CONTROL_TYPE(x, y) x,
#include "ControlType.inc"
#undef CONTROL_TYPE
  CT_MAX
} ControlType;

enum BlobColour {
  BLOB_NONE,
  BLOB_HISTORY_0,
  BLOB_HISTORY_1,
  BLOB_HISTORY_2,
  BLOB_HISTORY_3,
  BLOB_HISTORY_4,
  BLOB_HISTORY_5,
  BLOB_HISTORY_6,
  BLOB_HISTORY_7,
  BLOB_HISTORY_8,
  BLOB_HISTORY_9,
  BLOB_HISTORY_10,
  BLOB_HISTORY_11,
  BLOB_HISTORY_12,
  BLOB_HISTORY_13,
  BLOB_HISTORY_14,
  BLOB_HISTORY_15,
  BLOB_HISTORY_16,
  BLOB_HISTORY_17,
  BLOB_HISTORY_18,
  BLOB_HISTORY_19,
  BLOB_HISTORY_20,
  BLOB_HISTORY_21,
  BLOB_HISTORY_22,
  BLOB_HISTORY_23,
  BLOB_HISTORY_24,
  BLOB_HISTORY_25,
  BLOB_HISTORY_26,
  BLOB_HISTORY_27,
  BLOB_HISTORY_28,
  BLOB_HISTORY_29,
  BLOB_HISTORY_30,
  BLOB_HISTORY_31,
  BLOB_WEAK,
  BLOB_INTERMEDIATE,
  BLOB_STRONG
};
#define BLOB_HISTORY_MAX BLOB_HISTORY_31
#define BLOB_HISTORY_COLOURS (BLOB_HISTORY_MAX - BLOB_NONE)
#define BLOB_COLOURS (BLOB_STRONG + 1)

extern const char *convertRadarToString(int range_meters, int units, int index);
extern double local_distance(GeoPosition pos1, GeoPosition pos2);
extern double local_bearing(GeoPosition pos1, GeoPosition pos2);

enum DisplayModeType { DM_CHART_OVERLAY, DM_CHART_NONE };
enum ToolbarIconColor { TB_NONE, TB_HIDDEN, TB_SEARCHING, TB_SEEN, TB_STANDBY, TB_ACTIVE };
enum VariationSource { VARIATION_SOURCE_NONE, VARIATION_SOURCE_NMEA, VARIATION_SOURCE_FIX, VARIATION_SOURCE_WMM };
enum OpenGLMode { OPENGL_UNKOWN, OPENGL_OFF, OPENGL_ON };

static const bool HasBitCount2[8] = {
    false,  // 000
    false,  // 001
    false,  // 010
    true,   // 011
    false,  // 100
    true,   // 101
    true,   // 110
    true,   // 111
};

#define DEFAULT_OVERLAY_TRANSPARENCY (5)
#define MIN_OVERLAY_TRANSPARENCY (0)
#define MAX_OVERLAY_TRANSPARENCY (10)
#define MIN_AGE (4)
#define MAX_AGE (12)

enum RangeUnits { RANGE_MIXED, RANGE_METRIC, RANGE_NAUTIC };
static const int RangeUnitsToMeters[3] = {1852, 1000, 1852};

/**
 * The data that is stored in the opencpn.ini file. Most of this is set in the OptionsDialog,
 * some of it is 'secret' and can only be set by manipulating the ini file directly.
 */
struct PersistentSettings {
  size_t radar_count;                              // How many radars we have
  radar_control_item overlay_transparency;         // How transparent is the radar picture over the chart
  int range_index;                                 // index into range array, see RadarInfo.cpp
  int verbose;                                     // Loglevel 0..4.
  int guard_zone_threshold;                        // How many blobs must be sent by radar before we fire alarm
  int guard_zone_render_style;                     // 0 = Shading, 1 = Outline, 2 = Shading + Outline
  int guard_zone_timeout;                          // How long before we warn again when bogeys are found
  bool guard_zone_on_overlay;                      // 0 = false, 1 = true
  bool trails_on_overlay;                          // 0 = false, 1 = true
  int guard_zone_debug_inc;                        // Value to add on every cycle to guard zone bearings, for testing.
  double skew_factor;                              // Set to -1 or other value to correct skewing
  RangeUnits range_units;                          // See enum
  int max_age;                                     // Scans older than this in seconds will be removed
  radar_control_item timed_idle;                   // 0 = off, 1 = 5 mins, etc. to 7 = 35 mins
  radar_control_item idle_run_time;                // 0 = 10s, 1 = 30s, 2 = 1 min
  radar_control_item refreshrate;                  // How quickly to refresh the display
  int chart_overlay;                               // -1 = none, otherwise = radar number
  int menu_auto_hide;                              // 0 = none, 1 = 10s, 2 = 30s
  int drawing_method;                              // VertexBuffer, Shader, etc.
  bool developer_mode;                             // Readonly from config, allows head up mode
  bool show;                                       // whether to show any radar (overlay or window)
  bool show_radar[RADARS];                         // whether to show radar window
  bool show_radar_control[RADARS];                 // whether to show radar menu (control) window
  bool show_radar_target[RADARS];                  // whether to show AIS and ARPA targets on radar window
  bool transmit_radar[RADARS];                     // whether radar should be transmitting (persistent)
  bool pass_heading_to_opencpn;                    // Pass heading coming from radar as NMEA data to OpenCPN
  bool enable_cog_heading;                         // Allow COG as heading. Should be taken out back and shot.
  bool ignore_radar_heading;                       // For testing purposes
  bool reverse_zoom;                               // false = normal, true = reverse
  bool show_extreme_range;                         // Show red ring at extreme range and center
  int threshold_red;                               // Radar data has to be this strong to show as STRONG
  int threshold_green;                             // Radar data has to be this strong to show as INTERMEDIATE
  int threshold_blue;                              // Radar data has to be this strong to show as WEAK
  int threshold_multi_sweep;                       // Radar data has to be this strong not to be ignored in multisweep
  int type_detection_method;                       // 0 = default, 1 = ignore reports
  int AISatARPAoffset;                             // Rectangle side where to search AIS targets at ARPA position
  wxPoint control_pos[RADARS];                     // Saved position of control menu windows
  wxPoint window_pos[RADARS];                      // Saved position of radar windows, when floating and not docked
  wxPoint alarm_pos;                               // Saved position of alarm window
  wxString alert_audio_file;                       // Filepath of alarm audio file. Must be WAV.
  NetworkAddress radar_interface_address[RADARS];  // Saved address of radar. Used to speed up next boot.
  wxColour trail_start_colour;                     // Starting colour of a trail
  wxColour trail_end_colour;                       // Ending colour of a trail
  wxColour strong_colour;                          // Colour for STRONG returns
  wxColour intermediate_colour;                    // Colour for INTERMEDIATE returns
  wxColour weak_colour;                            // Colour for WEAK returns
  wxColour arpa_colour;                            // Colour for ARPA edges
  wxColour ais_text_colour;                        // Colour for AIS texts
  wxColour ppi_background_colour;                  // Colour for PPI background (normally very dark)
};

// Table for AIS targets inside ARPA zone
struct AisArpa {
  long ais_mmsi;
  time_t ais_time_upd;
  double ais_lat;
  double ais_lon;

  AisArpa() : ais_mmsi(0), ais_time_upd(), ais_lat(), ais_lon() {}
};

//----------------------------------------------------------------------------------------------------------
//    The PlugIn Class Definition
//----------------------------------------------------------------------------------------------------------

#define RADAR_TOOL_POSITION -1  // Request default positioning of toolbar tool

#define PLUGIN_OPTIONS                                                                                                       \
  (WANTS_DYNAMIC_OPENGL_OVERLAY_CALLBACK | WANTS_OPENGL_OVERLAY_CALLBACK | WANTS_OVERLAY_CALLBACK | WANTS_TOOLBAR_CALLBACK | \
   INSTALLS_TOOLBAR_TOOL | USES_AUI_MANAGER | WANTS_CONFIG | WANTS_NMEA_EVENTS | WANTS_NMEA_SENTENCES | WANTS_PREFERENCES |  \
   WANTS_PLUGIN_MESSAGING | WANTS_CURSOR_LATLON | WANTS_MOUSE_EVENTS)

class radar_pi : public opencpn_plugin_114, public wxEvtHandler {
 public:
  radar_pi(void *ppimgr);
  ~radar_pi();
  void PrepareRadarImage(int angle);

  //    The required PlugIn Methods
  int Init(void);
  bool DeInit(void);

  int GetAPIVersionMajor();
  int GetAPIVersionMinor();
  int GetPlugInVersionMajor();
  int GetPlugInVersionMinor();

  wxBitmap *GetPlugInBitmap();
  wxString GetCommonName();
  wxString GetShortDescription();
  wxString GetLongDescription();

  //    The required override PlugIn Methods
  bool RenderGLOverlay(wxGLContext *pcontext, PlugIn_ViewPort *vp);
  bool RenderOverlay(wxDC &dc, PlugIn_ViewPort *vp);
  void SetPositionFix(PlugIn_Position_Fix &pfix);
  void SetPositionFixEx(PlugIn_Position_Fix_Ex &pfix);
  void SetPluginMessage(wxString &message_id, wxString &message_body);
  void SetNMEASentence(wxString &sentence);
  void SetDefaults(void);
  int GetToolbarToolCount(void);
  void OnToolbarToolCallback(int id);
  void OnContextMenuItemCallback(int id);
  void ShowPreferencesDialog(wxWindow *parent);
  void SetCursorPosition(GeoPosition pos);
  void SetCursorLatLon(double lat, double lon);
  bool MouseEventHook(wxMouseEvent &event);
  bool m_guard_bogey_confirmed;

  // Other public methods

  bool IsRadarSelectionComplete(bool force);

  void NotifyRadarWindowViz();
  void NotifyControlDialog();

  void OnControlDialogClose(RadarInfo *ri);
  void SetDisplayMode(DisplayModeType mode);

  void ShowRadarControl(int radar, bool show = true, bool reparent = true);
  void ShowGuardZoneDialog(int radar, int zone);
  void OnGuardZoneDialogClose(RadarInfo *ri);
  void ConfirmGuardZoneBogeys();
  void ResetOpenGLContext();
  void logBinaryData(const wxString &what, const uint8_t *data, int size);

  void UpdateAllControlStates(bool all);
  bool SetControlValue(int radar, ControlType controlType, int value, int autoValue);

  bool IsRadarOnScreen(int radar) { return m_settings.show && (m_settings.show_radar[radar] || m_settings.chart_overlay == radar); }

  bool LoadConfig();
  bool SaveConfig();

  long GetRangeMeters();
  long GetOptimalRangeMeters();

  wxString GetTimedIdleText();
  wxString GetGuardZoneText(RadarInfo *ri);

  void SetRadarInterfaceAddress(int r, NetworkAddress &addr) {
    wxCriticalSectionLocker lock(m_exclusive);
    m_settings.radar_interface_address[r] = addr;
  };
  NetworkAddress &GetRadarInterfaceAddress(int r) {
    wxCriticalSectionLocker lock(m_exclusive);
    return m_settings.radar_interface_address[r];
  }

  void SetRadarHeading(double heading = nan(""), bool isTrue = false);
  double GetHeadingTrue() {
    wxCriticalSectionLocker lock(m_exclusive);
    return m_hdt;
  }
  time_t GetHeadingTrueTimeout() {
    wxCriticalSectionLocker lock(m_exclusive);
    return m_hdt_timeout;
  }
  time_t GetHeadingMagTimeout() {
    wxCriticalSectionLocker lock(m_exclusive);
    return m_hdm_timeout;
  }
  VariationSource GetVariationSource() {
    wxCriticalSectionLocker lock(m_exclusive);
    return m_var_source;
  }
  double GetCOG() {
    wxCriticalSectionLocker lock(m_exclusive);
    return m_cog;
  }
  HeadingSource GetHeadingSource() { return m_heading_source; }
  bool IsInitialized() { return m_initialized; }
  bool IsBoatPositionValid() {
    wxCriticalSectionLocker lock(m_exclusive);
    return m_bpos_set;
  }

  wxLongLong GetBootMillis() { return m_boot_time; }
  bool IsOpenGLEnabled() { return m_opengl_mode == OPENGL_ON; }
  wxGLContext *GetChartOpenGLContext();

  wxFont m_font;      // The dialog font at a normal size
  wxFont m_fat_font;  // The dialog font at a bigger size, bold

  PersistentSettings m_settings;
  RadarInfo *m_radar[RADARS];
  wxString m_perspective[RADARS];  // Temporary storage of window location when plugin is disabled

  MessageBox *m_pMessageBox;
  wxWindow *m_parent_window;

  // Check for AIS targets inside ARPA zone
  vector<AisArpa> m_ais_in_arpa_zone;  // Array for AIS targets in ARPA zone(s)
  bool FindAIS_at_arpaPos(const GeoPosition &pos, const double &dist);

 private:
  void RadarSendState(void);
  void UpdateState(void);
  void UpdateHeadingPositionState(void);
  void DoTick(void);
  void Select_Clutter(int req_clutter_index);
  void Select_Rejection(int req_rejection_index);
  void CheckGuardZoneBogeys(void);
  void RenderRadarBuffer(wxDC *pdc, int width, int height);
  void PassHeadingToOpenCPN();
  void CacheSetToolbarToolBitmaps();
  void CheckTimedTransmit(RadarState state);
  void RequestStateAllRadars(RadarState state);
  void SetRadarWindowViz(bool reparent = false);
  void UpdateContextMenu();
  void UpdateCOGAvg(double cog);
  void OnTimerNotify(wxTimerEvent &event);
  void TimedControlUpdate();
  void ScheduleWindowRefresh();
  void SetOpenGLMode(OpenGLMode mode);

  wxCriticalSection m_exclusive;  // protects callbacks that come from multiple radars

  double m_hdt;                    // this is the heading that the pi is using for all heading operations, in degrees.
                                   // m_hdt will come from the radar if available else from the NMEA stream.
  time_t m_hdt_timeout;            // When we consider heading is lost
  double m_hdm;                    // Last magnetic heading obtained
  time_t m_hdm_timeout;            // When we consider heading is lost
  double m_radar_heading;          // Last heading obtained from radar, or nan if none
  bool m_radar_heading_true;       // Was TRUE flag set on radar heading?
  time_t m_radar_heading_timeout;  // When last heading was obtained from radar, or 0 if not
  HeadingSource m_heading_source;
  bool m_bpos_set;
  time_t m_bpos_timestamp;

  // Variation. Used to convert magnetic into true heading.
  // Can come from SetPositionFixEx, which may hail from the WMM plugin
  // and is thus to be preferred, or GPS or a NMEA sentence. The latter will probably
  // have an outdated variation model, so is less preferred. Besides, some devices
  // transmit invalid (zero) values. So we also let non-zero values prevail.
  double m_var;  // local magnetic variation, in degrees
  VariationSource m_var_source;
  time_t m_var_timeout;

  wxFileConfig *m_pconfig;
  int m_context_menu_control_id;
  int m_context_menu_show_id;
  int m_context_menu_hide_id;
  int m_context_menu_acquire_radar_target;
  int m_context_menu_delete_radar_target;
  int m_context_menu_delete_all_radar_targets;

  int m_tool_id;
  wxBitmap *m_pdeficon;

  //    Controls added to Preferences panel
  wxCheckBox *m_pShowIcon;

  // Icons
  wxString m_shareLocn;
  // wxBitmap *m_ptemp_icon;

  NMEA0183 m_NMEA0183;

  ToolbarIconColor m_toolbar_button;
  ToolbarIconColor m_sent_toolbar_button;

  bool m_old_data_seen;
  volatile bool m_notify_radar_window_viz;
  volatile bool m_notify_control_dialog;
  wxLongLong m_notify_time_ms;

#define HEADING_TIMEOUT (5)

  GuardZoneBogey *m_bogey_dialog;
  bool m_guard_bogey_seen;  // Saw guardzone bogeys on last check
  time_t m_alarm_sound_timeout;
  time_t m_guard_bogey_timeout;  // If we haven't seen bogeys for this long we reset confirm
#define CONFIRM_RESET_TIMEOUT (15)

// Compute average COG same way as OpenCPN
#define MAX_COG_AVERAGE_SECONDS (60)
  double m_COGTable[MAX_COG_AVERAGE_SECONDS];
  int m_COGAvgSec;       // Default 15, comes from OCPN settings
  double m_COGAvg;       // Average COG over m_COGTable
  double m_cog;          // Value of m_COGAvg at rotation time
  time_t m_cog_timeout;  // When m_cog will be set again
  double m_vp_rotation;  // Last seen vp->rotation

  // Keep last state of ContextMenu state sent, to avoid redraws
  bool m_context_menu_show;
  bool m_context_menu_control;
  bool m_context_menu_arpa;

  // Cursor position. Used to show position in radar window
  GeoPosition m_cursor_pos;
  GeoPosition m_ownship;

  bool m_initialized;      // True if Init() succeeded and DeInit() not called yet.
  bool m_first_init;       // True in first Init() call.
  wxLongLong m_boot_time;  // millis when started

  // Timed Transmit
  time_t m_idle_standby;   // When we will change to standby
  time_t m_idle_transmit;  // When we will change to transmit

  OpenGLMode m_opengl_mode;
  volatile bool m_opengl_mode_changed;

  wxGLContext *m_opencpn_gl_context;
  bool m_opencpn_gl_context_broken;

  wxTimer *m_timer;

  DECLARE_EVENT_TABLE()
};

PLUGIN_END_NAMESPACE

#include "ControlsDialog.h"
#include "GuardZone.h"
#include "MessageBox.h"
#include "OptionsDialog.h"
#include "RadarControl.h"
#include "RadarInfo.h"

#endif /* _RADAR_PI_H_ */
