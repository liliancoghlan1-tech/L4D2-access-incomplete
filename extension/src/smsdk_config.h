#ifndef _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_
#define _INCLUDE_SOURCEMOD_EXTENSION_CONFIG_H_

#define SMEXT_CONF_NAME         "NVDA Accessibility"
#define SMEXT_CONF_DESCRIPTION  "NVDA screen reader integration for L4D2"
#define SMEXT_CONF_VERSION      "1.0.0"
#define SMEXT_CONF_AUTHOR       "L4D2 Accessibility Mod"
#define SMEXT_CONF_URL          ""
#define SMEXT_CONF_LOGTAG       "NVDA"
#define SMEXT_CONF_LICENSE      "GPL"
#define SMEXT_CONF_DATESTRING   __DATE__

#define SMEXT_LINK(name) SDKExtension *g_pExtensionIface = name;

#endif
