// =============================================================================
// Translation.h — VolumioRemote localisation
// =============================================================================
//
// HOW TO USE
// ----------
// 1. Open Language.h and uncomment ONE language only:
//
//      #define LANG_EN   // English  (default)
//      #define LANG_FR   // French
//      #define LANG_DE   // German
//      #define LANG_ES   // Spanish
//      #define LANG_NL   // Dutch
//      #define LANG_PT   // Portuguese
//
// 2. Recompile and flash. That's it!

#ifndef TRANSLATION_H
#define TRANSLATION_H

// -----------------------------------------------------------------------------
// Default to English if nothing is defined
// -----------------------------------------------------------------------------
#if !defined(LANG_EN) && !defined(LANG_FR) && !defined(LANG_DE) && \
    !defined(LANG_ES) && !defined(LANG_NL) && !defined(LANG_PT) && \
    !defined(LANG_TR)
  #define LANG_EN
#endif

// =============================================================================
//  ENGLISH
// =============================================================================
#ifdef LANG_EN

// ── Device (TFT) ──────────────────────────────────────────────────────────
const char strWaitingForVolumio[] = "Waiting for Volumio...";
const char strVolumioUnreachable[] = "Volumio unreachable at ";  // + host, appended in code
const char strVol[]               = "Vol ";
const char strTouchCalibrate[]    = "Touch each crosshair to calibrate";
const char strTouchCalCleared[]   = "Touch calibration cleared. Rebooting...";
const char strConnectingWifi[]    = "Connecting to saved WiFi...";
const char strSetupTimedOut[]     = "Setup timed out - rebooting";
const char strConnected[]         = "Connected: ";  // + IP, appended in code
const char strWifiCleared[]       = "WiFi settings cleared. Rebooting...";
const char strApActive[]          = "Access Point Active";
const char strStationListTitle[]  = "Favorite Stations";
const char strStationsLoading[]   = "Loading stations...";
const char strUsbLoading[]        = "Loading USB Music...";  // USB browsing: distinct from strStationsLoading, which is favorites-only wording
const char strNoStations[]        = "No favorite stations found";
const char strUsbFolderEmpty[]    = "No songs found here";  // USB browsing: this folder has no playable tracks directly in it
const char strChooseSourceTitle[] = "Choose Source";
const char strUsbMusicTitle[]     = "USB Music";
const char strChoiceWebRadio[]    = "Web Radio Favorites";
const char strChoiceUsbMusic[]    = "USB Music";

// ── Web UI ────────────────────────────────────────────────────────────────
const char wcPageTitle[]          = "Volumio Remote Settings";
const char wcHeading[]            = "Volumio Remote";
const char wcDeviceAt[]           = "Device at ";  // + IP, appended in code
const char wcSecVolumioHost[]     = "Volumio Host";
const char wcNameOrIp[]           = "Name or IP";
const char wcTest[]               = "Test";
const char wcSecCoverArt[]        = "Cover Art Source";
const char wcDiscogsToken[]       = "Discogs API Token";
const char wcDiscogsPlaceholder[] = "Paste your Discogs personal token";
const char wcSecScreensaver[]     = "Screensaver";
const char wcStyle[]              = "Style";
const char wcTime[]               = "Time";
const char wcUpdate[]             = "Update";
const char wcSecDevice[]          = "Device";
const char wcRecalTouch[]         = "Recalibrate Touch Screen";
const char wcRecalConfirm[]       = "Recalibrate the touch screen? The device will reboot into calibration mode.";
const char wcResetWifi[]          = "Reset WiFi Settings";
const char wcResetWifiConfirm[]   = "Forget the saved WiFi network? The device will reboot into setup mode.";
const char wcSettingsSaved[]      = "Settings saved";
const char wcRebooting[]          = "Rebooting...";

#endif // LANG_EN

// =============================================================================
//  FRENCH
// =============================================================================
#ifdef LANG_FR

const char strWaitingForVolumio[] = "En attente de Volumio...";
const char strVolumioUnreachable[] = "Volumio injoignable à ";
const char strVol[]               = "Vol ";
const char strTouchCalibrate[]    = "Touchez chaque repère pour calibrer";
const char strTouchCalCleared[]   = "Étalonnage tactile effacé. Redémarrage...";
const char strConnectingWifi[]    = "Connexion au Wi-Fi enregistré...";
const char strSetupTimedOut[]     = "Délai de configuration dépassé - redémarrage";
const char strConnected[]         = "Connecté : ";
const char strWifiCleared[]       = "Wi-Fi réinitialisé. Redémarrage...";
const char strApActive[]          = "Point d'accès actif";
const char strStationListTitle[]  = "Stations favorites";
const char strStationsLoading[]   = "Chargement des stations...";
const char strUsbLoading[]        = "Chargement de la musique USB...";
const char strNoStations[]        = "Aucune station favorite trouvée";
const char strUsbFolderEmpty[]    = "Aucun morceau trouvé ici";
const char strChooseSourceTitle[] = "Choisir la source";
const char strUsbMusicTitle[]     = "Musique USB";
const char strChoiceWebRadio[]    = "Radios favorites";
const char strChoiceUsbMusic[]    = "Musique USB";

const char wcPageTitle[]          = "Réglages Volumio Remote";
const char wcHeading[]            = "Volumio Remote";
const char wcDeviceAt[]           = "Appareil à ";
const char wcSecVolumioHost[]     = "Hôte Volumio";
const char wcNameOrIp[]           = "Nom ou IP";
const char wcTest[]               = "Tester";
const char wcSecCoverArt[]        = "Source de la pochette";
const char wcDiscogsToken[]       = "Jeton API Discogs";
const char wcDiscogsPlaceholder[] = "Collez votre jeton personnel Discogs";
const char wcSecScreensaver[]     = "Économiseur d'écran";
const char wcStyle[]              = "Style";
const char wcTime[]               = "Durée";
const char wcUpdate[]             = "Mettre à jour";
const char wcSecDevice[]          = "Appareil";
const char wcRecalTouch[]         = "Recalibrer l'écran tactile";
const char wcRecalConfirm[]       = "Recalibrer l'écran tactile ? L'appareil va redémarrer en mode calibration.";
const char wcResetWifi[]          = "Réinitialiser le Wi-Fi";
const char wcResetWifiConfirm[]   = "Oublier le réseau Wi-Fi enregistré ? L'appareil va redémarrer en mode configuration.";
const char wcSettingsSaved[]      = "Réglages enregistrés";
const char wcRebooting[]          = "Redémarrage...";

#endif // LANG_FR

// =============================================================================
//  GERMAN
// =============================================================================
#ifdef LANG_DE

const char strWaitingForVolumio[] = "Warte auf Volumio...";
const char strVolumioUnreachable[] = "Volumio nicht erreichbar unter ";
const char strVol[]               = "Vol ";
const char strTouchCalibrate[]    = "Jedes Fadenkreuz zum Kalibrieren berühren";
const char strTouchCalCleared[]   = "Touch-Kalibrierung gelöscht. Neustart...";
const char strConnectingWifi[]    = "Verbinde mit gespeichertem WLAN...";
const char strSetupTimedOut[]     = "Zeitüberschreitung beim Einrichten - Neustart";
const char strConnected[]         = "Verbunden: ";
const char strWifiCleared[]       = "WLAN-Einstellungen gelöscht. Neustart...";
const char strApActive[]          = "Zugangspunkt aktiv";
const char strStationListTitle[]  = "Lieblingssender";
const char strStationsLoading[]   = "Sender werden geladen...";
const char strUsbLoading[]        = "USB-Musik wird geladen...";
const char strNoStations[]        = "Keine Lieblingssender gefunden";
const char strUsbFolderEmpty[]    = "Keine Titel hier gefunden";
const char strChooseSourceTitle[] = "Quelle wählen";
const char strUsbMusicTitle[]     = "USB-Musik";
const char strChoiceWebRadio[]    = "Webradio-Favoriten";
const char strChoiceUsbMusic[]    = "USB-Musik";

const char wcPageTitle[]          = "Volumio Remote Einstellungen";
const char wcHeading[]            = "Volumio Remote";
const char wcDeviceAt[]           = "Gerät unter ";
const char wcSecVolumioHost[]     = "Volumio-Host";
const char wcNameOrIp[]           = "Name oder IP";
const char wcTest[]               = "Testen";
const char wcSecCoverArt[]        = "Cover-Quelle";
const char wcDiscogsToken[]       = "Discogs-API-Token";
const char wcDiscogsPlaceholder[] = "Persönliches Discogs-Token einfügen";
const char wcSecScreensaver[]     = "Bildschirmschoner";
const char wcStyle[]              = "Stil";
const char wcTime[]               = "Zeit";
const char wcUpdate[]             = "Aktualisieren";
const char wcSecDevice[]          = "Gerät";
const char wcRecalTouch[]         = "Touchscreen neu kalibrieren";
const char wcRecalConfirm[]       = "Touchscreen neu kalibrieren? Das Gerät startet neu in den Kalibriermodus.";
const char wcResetWifi[]          = "WLAN-Einstellungen zurücksetzen";
const char wcResetWifiConfirm[]   = "Gespeichertes WLAN vergessen? Das Gerät startet neu in den Einrichtungsmodus.";
const char wcSettingsSaved[]      = "Einstellungen gespeichert";
const char wcRebooting[]          = "Neustart...";

#endif // LANG_DE

// =============================================================================
//  SPANISH
// =============================================================================
#ifdef LANG_ES

const char strWaitingForVolumio[] = "Esperando a Volumio...";
const char strVolumioUnreachable[] = "Volumio no disponible en ";
const char strVol[]               = "Vol ";
const char strTouchCalibrate[]    = "Toca cada mira para calibrar";
const char strTouchCalCleared[]   = "Calibración táctil borrada. Reiniciando...";
const char strConnectingWifi[]    = "Conectando al Wi-Fi guardado...";
const char strSetupTimedOut[]     = "Tiempo de configuración agotado - reiniciando";
const char strConnected[]         = "Conectado: ";
const char strWifiCleared[]       = "Wi-Fi restablecido. Reiniciando...";
const char strApActive[]          = "Punto de acceso activo";
const char strStationListTitle[]  = "Emisoras favoritas";
const char strStationsLoading[]   = "Cargando emisoras...";
const char strUsbLoading[]        = "Cargando música USB...";
const char strNoStations[]        = "No se encontraron emisoras favoritas";
const char strUsbFolderEmpty[]    = "No se encontraron canciones aquí";
const char strChooseSourceTitle[] = "Elegir fuente";
const char strUsbMusicTitle[]     = "Música USB";
const char strChoiceWebRadio[]    = "Emisoras favoritas";
const char strChoiceUsbMusic[]    = "Música USB";

const char wcPageTitle[]          = "Ajustes de Volumio Remote";
const char wcHeading[]            = "Volumio Remote";
const char wcDeviceAt[]           = "Dispositivo en ";
const char wcSecVolumioHost[]     = "Host de Volumio";
const char wcNameOrIp[]           = "Nombre o IP";
const char wcTest[]               = "Probar";
const char wcSecCoverArt[]        = "Origen de la carátula";
const char wcDiscogsToken[]       = "Token de API de Discogs";
const char wcDiscogsPlaceholder[] = "Pega tu token personal de Discogs";
const char wcSecScreensaver[]     = "Salvapantallas";
const char wcStyle[]              = "Estilo";
const char wcTime[]               = "Tiempo";
const char wcUpdate[]             = "Actualizar";
const char wcSecDevice[]          = "Dispositivo";
const char wcRecalTouch[]         = "Recalibrar pantalla táctil";
const char wcRecalConfirm[]       = "¿Recalibrar la pantalla táctil? El dispositivo se reiniciará en modo de calibración.";
const char wcResetWifi[]          = "Restablecer ajustes de Wi-Fi";
const char wcResetWifiConfirm[]   = "¿Olvidar la red Wi-Fi guardada? El dispositivo se reiniciará en modo de configuración.";
const char wcSettingsSaved[]      = "Ajustes guardados";
const char wcRebooting[]          = "Reiniciando...";

#endif // LANG_ES

// =============================================================================
//  DUTCH
// =============================================================================
#ifdef LANG_NL

const char strWaitingForVolumio[] = "Wachten op Volumio...";
const char strVolumioUnreachable[] = "Volumio niet bereikbaar op ";
const char strVol[]               = "Vol ";
const char strTouchCalibrate[]    = "Raak elk kruis aan om te kalibreren";
const char strTouchCalCleared[]   = "Touchkalibratie gewist. Opnieuw opstarten...";
const char strConnectingWifi[]    = "Verbinden met opgeslagen wifi...";
const char strSetupTimedOut[]     = "Instellen verlopen - opnieuw opstarten";
const char strConnected[]         = "Verbonden: ";
const char strWifiCleared[]       = "Wifi-instellingen gewist. Opnieuw opstarten...";
const char strApActive[]          = "Toegangspunt actief";
const char strStationListTitle[]  = "Favoriete zenders";
const char strStationsLoading[]   = "Zenders laden...";
const char strUsbLoading[]        = "USB-muziek laden...";
const char strNoStations[]        = "Geen favoriete zenders gevonden";
const char strUsbFolderEmpty[]    = "Geen nummers gevonden";
const char strChooseSourceTitle[] = "Bron kiezen";
const char strUsbMusicTitle[]     = "USB-muziek";
const char strChoiceWebRadio[]    = "Favoriete zenders";
const char strChoiceUsbMusic[]    = "USB-muziek";

const char wcPageTitle[]          = "Volumio Remote-instellingen";
const char wcHeading[]            = "Volumio Remote";
const char wcDeviceAt[]           = "Apparaat op ";
const char wcSecVolumioHost[]     = "Volumio-host";
const char wcNameOrIp[]           = "Naam of IP";
const char wcTest[]               = "Testen";
const char wcSecCoverArt[]        = "Bron albumhoes";
const char wcDiscogsToken[]       = "Discogs API-token";
const char wcDiscogsPlaceholder[] = "Plak je persoonlijke Discogs-token";
const char wcSecScreensaver[]     = "Schermbeveiliging";
const char wcStyle[]              = "Stijl";
const char wcTime[]               = "Tijd";
const char wcUpdate[]             = "Bijwerken";
const char wcSecDevice[]          = "Apparaat";
const char wcRecalTouch[]         = "Touchscreen opnieuw kalibreren";
const char wcRecalConfirm[]       = "Touchscreen opnieuw kalibreren? Het apparaat start opnieuw op in kalibratiemodus.";
const char wcResetWifi[]          = "Wifi-instellingen resetten";
const char wcResetWifiConfirm[]   = "Opgeslagen wifinetwerk vergeten? Het apparaat start opnieuw op in installatiemodus.";
const char wcSettingsSaved[]      = "Instellingen opgeslagen";
const char wcRebooting[]          = "Opnieuw opstarten...";

#endif // LANG_NL

// =============================================================================
//  PORTUGUESE
// =============================================================================
#ifdef LANG_PT

const char strWaitingForVolumio[] = "Aguardando o Volumio...";
const char strVolumioUnreachable[] = "Volumio inacessível em ";
const char strVol[]               = "Vol ";
const char strTouchCalibrate[]    = "Toque em cada mira para calibrar";
const char strTouchCalCleared[]   = "Calibração do toque apagada. Reiniciando...";
const char strConnectingWifi[]    = "Conectando ao Wi-Fi salvo...";
const char strSetupTimedOut[]     = "Tempo de configuração esgotado - reiniciando";
const char strConnected[]         = "Conectado: ";
const char strWifiCleared[]       = "Configurações de Wi-Fi apagadas. Reiniciando...";
const char strApActive[]          = "Ponto de acesso ativo";
const char strStationListTitle[]  = "Estações favoritas";
const char strStationsLoading[]   = "Carregando estações...";
const char strUsbLoading[]        = "Carregando música USB...";
const char strNoStations[]        = "Nenhuma estação favorita encontrada";
const char strUsbFolderEmpty[]    = "Nenhuma música encontrada aqui";
const char strChooseSourceTitle[] = "Escolher fonte";
const char strUsbMusicTitle[]     = "Música USB";
const char strChoiceWebRadio[]    = "Estações favoritas";
const char strChoiceUsbMusic[]    = "Música USB";

const char wcPageTitle[]          = "Configurações do Volumio Remote";
const char wcHeading[]            = "Volumio Remote";
const char wcDeviceAt[]           = "Dispositivo em ";
const char wcSecVolumioHost[]     = "Host do Volumio";
const char wcNameOrIp[]           = "Nome ou IP";
const char wcTest[]               = "Testar";
const char wcSecCoverArt[]        = "Origem da capa";
const char wcDiscogsToken[]       = "Token de API do Discogs";
const char wcDiscogsPlaceholder[] = "Cole seu token pessoal do Discogs";
const char wcSecScreensaver[]     = "Proteção de tela";
const char wcStyle[]              = "Estilo";
const char wcTime[]               = "Tempo";
const char wcUpdate[]             = "Atualizar";
const char wcSecDevice[]          = "Dispositivo";
const char wcRecalTouch[]         = "Recalibrar tela sensível ao toque";
const char wcRecalConfirm[]       = "Recalibrar a tela sensível ao toque? O dispositivo será reiniciado no modo de calibração.";
const char wcResetWifi[]          = "Redefinir configurações de Wi-Fi";
const char wcResetWifiConfirm[]   = "Esquecer a rede Wi-Fi salva? O dispositivo será reiniciado no modo de configuração.";
const char wcSettingsSaved[]      = "Configurações salvas";
const char wcRebooting[]          = "Reiniciando...";

#endif // LANG_PT

#endif // TRANSLATION_H
