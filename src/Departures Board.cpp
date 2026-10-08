/*
 * Departures Board (c) 2025-2026 Gadec Software
 *
 * https://github.com/gadec-uk/departures-board
 *
 * This work is licensed under Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International.
 * To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/
 *
 * ESP32 "Mini" Board with 3.12" 256x64 OLED Display Panel with SSD1322 controller on-board.
 *
 * OLED PANEL     ESP32 MINI
 * 1 VSS          GND
 * 2 VCC_IN       3.3V
 * 4 D0/CLK       IO18
 * 5 D1/DIN       IO23
 * 14 D/C#        IO5
 * 16 CS#         IO26
 *
 * Optional TTP223 touch sensor connection:
 *
 * TTP223         ESP32 MINI
 * GND            GND
 * VCC            3.3V
 * I/O            IO34
 *
 * ESP32 Cheap Yellow Display (ESP32-2432S028R) with its integrated
 * 320x240 TFT and XPT2046 touchscreen.
 * Referenced from https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display
 * Wiring and usage instructions can be found in the referenced repository.
 * 
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <StreamString.h>
#include <Ticker.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <HTTPUpdateGitHub.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <weatherClient.h>
#include <sharedDataStructs.h>
#include <responseCodes.h>
#include <raildataXmlClient.h>
#include <rdmRailClient.h>
#include <TfLdataClient.h>
#include <busDataClient.h>
#include <githubClient.h>
#include <rssClient.h>
#include <touchSensor.h>
#include <webgui/webgraphics.h>
#include <webgui/index.h>
#include <webgui/live.h>
#include <webgui/departures.h>
#include <webgui/keys.h>
#include <webgui/editrss.h>
#include <webgui/rss.h>
#include <gfx/xbmgfx.h>
#include <time.h>
#include <array>

#include <SPI.h>
#include <U8g2lib.h>

#define msDay 86400000 // 86400000 milliseconds in a day
#define msHour 3600000 // 3600000 milliseconds in an hour
#define msMin 60000 // 60000 milliseconds in a second

static AsyncWebServer server(80); // Hosting the Web GUI

// Shorthand for response formats
static const char contentTypeJson[] = "application/json";
static const char contentTypeText[] = "text/plain";
static const char contentTypeHtml[] = "text/html";

// Using NTP to set and maintain the clock
static struct tm timeinfo;
static const char ukTimezone[] = "GMT0BST,M3.5.0/1,M10.5.0";

// Default hostname
static const char defaultHostname[] = "DeparturesBoard";

// Local firmware updates via /update Web GUI
static const char updatePage[] =
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<html><body style=\"font-family:Helvetica,Arial,sans-serif\"><h2>Departures Board Manual Update</h2><p>Upload a <b>firmware.bin</b> file.</p>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>"
 "<div id='prg'>progress: 0%</div>"
 "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '/update',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('Progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!')"
 "},"
 "error: function (a, b, c) {"
 "}"
 "});"
 "});"
 "</script></body></html>";

// /upload page
static const char uploadPage[] =
"<html><body style=\"font-family:Helvetica,Arial,sans-serif\">"
"<h2>Upload a file to the file system</h2><form method='post' enctype='multipart/form-data'><input type='file' name='name'>"
"<input class='button' type='submit' value='Upload'></form></body></html>";

// /success page
static const char successPage[] =
"<html><body style=\"font-family:Helvetica,Arial,sans-serif\"><h3>Upload completed successfully.</h3>\n"
"<p><a href=\"/dir\">List file system directory</a></p>\n"
"<h2>Upload another file</h2><form method=\"post\" action=\"/upload\" enctype=\"multipart/form-data\"><input type=\"file\" name=\"name\"><input class=\"button\" type=\"submit\" value=\"Upload\"></form>\n"
"</body></html>";

#if defined(DISPLAY_CYD)
#define SCREEN_WIDTH 320 // Native CYD canvas width, in pixels
#define SCREEN_HEIGHT 240 // Native CYD canvas height, in pixels
#else
#define SCREEN_WIDTH 256 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#endif

#if defined(DISPLAY_CYD)
#define DIMMED_BRIGHTNESS 20 // CYD display brightness level when in sleep/screensaver mode (0-255)
#include "cydDisplay.h"
U8G2_CYD_TFT u8g2;
#else
#define DIMMED_BRIGHTNESS 1 // OLED display brightness level when in sleep/screensaver mode
U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ GPIO_NUM_26, /* dc=*/ GPIO_NUM_5, /* reset=*/ U8X8_PIN_NONE);
#endif

#if defined(DISPLAY_CYD)
// Vertical line positions on the native CYD display (National Rail)
#define LINE0 0
#define LINE1 64
#define PRIMARY_MESSAGE_LINE 108
#define LINE2 130
#define LINE3 174
#define LINE4 204

// Vertical line positions on the native CYD display (Underground)
#define ULINE0 0
#define ULINE1 80
#define ULINE2 116
#define ULINE3 152
#define ULINE4 188
#else
// Vertical line positions on the OLED display (National Rail)
#define LINE0 0
#define LINE1 13
#define LINE2 28
#define LINE3 41
#define LINE4 55

// Vertical line positions on the OLED display (Underground)
#define ULINE0 0
#define ULINE1 15
#define ULINE2 28
#define ULINE3 41
#define ULINE4 56
#endif

static Ticker restartTimer; // used to schedule reboots

//
// Custom fonts - replicas of those used on the real display boards
//
static const uint8_t NatRailSmall9[1329] U8G2_FONT_SECTION("NatRailSmall9") =
  "\221\0\3\2\4\4\4\5\5\11\11\0\0\11\0\11\2\1E\2\214\5\30 \5\0\230)!\7q("
  "%\6%\42\10\63h)\22K\0#\17u(mJI\62(\225A)%\11\0$\14u(\255\262"
  "\245\266%\311\26\1%\14t(+\42E\212\244H\221\2&\16u(m\244\222\224$R\22EJ\0"
  "'\6\61h%\6(\11s(\251\222R\255\0)\11s()\262R\245\4*\12U\70-\222\312b"
  "Y\32+\12U\70\255\302h\220\302\10,\7\62\10g\22\5-\6\23X)\6.\6\21(%\2/"
  "\13t(\353\42)\222\42)\3\60\12u(m\226\314[\262\0\61\11s)m\22\251\313\0\62\13u"
  "(m\226,\314\332\6\1\63\14u(m\226,\214T-Y\0\64\15u(\355\62))%\203\26&"
  "\0\65\13u(-\216C\32j\311\2\66\15u(m\226L\34\222LK\26\0\67\12u(-\6\61"
  "+\66\1\70\15u(m\226LK\226LK\26\0\71\15u(m\226LK\206PK\26\0:\7A"
  "\70%\242\0;\7R(gR\5<\10t(\353\242\306\6=\10\64H+\206p\10>\11t(+"
  "\302\246\66\0?\14u(m\226,\214\264\34\212\0@\14u(m\226\314\222(C\272\0A\13u("
  "m\226L\33\206\314\26B\15u(-\206$\323\6%\323\6\5C\13u(m\226LlK\26\0D"
  "\13u(-\206$\363\66(\0E\13u(-\216\341\220\204\341 F\13u(-\216\341\220\204E\0"
  "G\15u(m\226LL\206LK\26\0H\13u(-\62\333\60d\266\0I\7q(%\16\1J"
  "\11u(-;j\311\2K\15u(-\62))iIT\311\2L\11u(-\302\36\7\1M\13"
  "u(-\262eI\64\267\0N\13u(-\262II\244\315\26O\12u(m\226\314[\262\0P\14"
  "u(-\206$\323\6%,\2Q\13\225\10m\226\314[\62\246\1R\15u(-\206$\323\6\245T"
  "\311\2S\13u(m\226L]\265d\1T\11u(-\6)\354\11U\11u(-\62\337\222\5V"
  "\12u(-\62oI-\2W\13u(-\62\227DIn\1X\14u(-\62-\251UjZ\0"
  "Y\13u(-\62-\251\205M\0Z\12u(-\6\61\353\70\10[\10r('\226.\2\134\12t"
  "(+\62-\323\62-]\10r(\63\224.\3^\6#xi\32_\7\24\30+\206\0`\7\42x"
  "'\242\0a\12U(m\326d\320\222!b\13u(-\302\212I\323\6\5c\11U(m\6\261:"
  "\4d\13u(-+\246MK\206\0e\12U(m\226l\30\322!f\12t(\253*\245)+\1"
  "g\14u\10m\6\315\226\14\341\240\0h\12u(-\302\212I\263\5i\7q(%\222Aj\13\224"
  "\10\353r k\223\22\5k\13t(+\262\222\222HI)l\7q(%\16\1m\12U(m\272"
  "(\211\246\5n\11U(-\22\223f\13o\12U(m\226\314\226,\0p\14u\10-\206$\263\15"
  "J\30\2q\13u\10m\6\315\226\14a\1r\11U(-\22\223X\4s\11U(m\6\365\240\0"
  "t\13t(k\262hH\262\242\0u\11U(-\62'E\11v\12U(-\62[R\213\0w\14"
  "U(-\62%Q\22\245\13\0x\12U(-\262\244V\251\5y\13u\10-\62\267d\10\7\5z"
  "\11U(-\6\255m\20{\12s(\251\222(\311\242,|\7q(%\16\1}\13s()\262("
  "K\242$\2~\10%XmL\11\0\15u(-\222\254R\214\222(Q\12\200\5\0\10!\201\14"
  "e)\361\222A\213\262A\311\0\202\10B\10g*\12\0\203\5\0\10!\204\11D\10kzQ\22\0"
  "\205\7\25(-\222\2\206\15v(o\206$\64\16\17I\224\0\207\16w(\261\266\244)\262\250I\226"
  "M\0\210\21\210\30\63\323!\213\222d\213\262\245\224\15i\10\211\5\0\10!\212\5\0\10!\213\5\0"
  "\10!\214\5\0\10!\215\15u(/NK\242)C\62\14\2\216\16u(/\16\221\62(\203\42\15"
  "C\0\217\14w(\61\336\246\310\242n\303\1\220\6\63N?\36\221\7\62h'\222(\222\7\62hg"
  "\22\5\223\10\64h+\222\246$\224\10\64hkZ\224\4\225\6\63H)\36\226\7\25X-\6\1\227"
  "\5\0\10!\230\5\0\10!\231\5\0\10!\232\5\0\10!\233\5\0\10!\234\5\0\10!\235\5\0"
  "\10!\236\5\0\10!\237\5\0\10!\240\5\0\10!\241\5\0\10!\242\5\0\10!\243\14u(\255"
  "\244J\66Da\66\10\244\5\0\10!\245\5\0\10!\246\5\0\10!\247\5\0\10!\250\5\0\10!"
  "\251\5\0\10!\252\5\0\10!\253\5\0\10!\254\5\0\10!\255\5\0\10!\256\5\0\10!\257\5"
  "\0\10!\260\12DXk\224HJ\24\0\0\0\0";

static uint8_t NatRailTall12[1102] U8G2_FONT_SECTION("NatRailTall12") =
  "c\0\3\2\4\4\2\5\5\11\14\0\375\11\375\11\0\1Q\2\235\4\65 \5\0f\12!\7\221B"
  "\211C\22\42\7#^\212D\11#\21\225B\233R\222\14J)\211\222dPJI\2$\17\225B\253"
  "l)%\331\226DI\262E\0%\12\225B\313i\312:\35\1&\20\225B\33\251\22%Q$%\211"
  "\224D\221\22'\6!^\11\1(\11\223B\252\244\324\255\0)\11\223B\212\254\324\245\4*\14uF"
  "\253Je\261,M\21\0+\12UJ\253\60\32\244\60\2,\7\62\272\231D\1-\6\23R\212\1."
  "\6!B\11\1/\11\225B\313\266\216E\0\60\12\225B\233%\363[\262\0\61\11\223C\233D\352\313"
  "\0\62\13\225B\233%\13k\35\7\1\63\15\225B\233%\13Kj\250%\13\0\64\16\225B\273LJ"
  "JI\224\14ZX\1\65\15\225B\213c\70\244a\250%\13\0\66\15\225B\233%\23\303!\311l\311"
  "\2\67\15\225B\213A\254\205Y\230\205\31\0\70\15\225B\233%\263%KfK\26\0\71\15\225B\233"
  "%\263%C\30j\311\2:\6QJ\11E;\10b\306\231Z\242\0<\10t\306\272\250\261\1=\10"
  "\64\316\212!\34\2>\10t\306\212\260\251\15?\14\225B\233%\13k\305\34\212\0@\16\225B\233%"
  "\263$J\242\14a\272\0A\13\225B\233%\263\15C\346\26B\15\225B\213!\311l\203\222\331\6\5"
  "C\13\225B\233%\23{K\26\0D\13\225B\213!\311\374\66(\0E\13\225B\213cqH\302\342"
  " F\13\225B\213cqH\302F\0G\14\225B\233%\23K\233-Y\0H\13\225B\213\314m\30"
  "\62\267\0I\6\221B\211\7J\11\225B\313>j\311\2K\16\225B\213\60\223\222\222\246%Q%\13"
  "L\11\225B\213\260\37\7\1M\14\225B\213lY\22%\321\274\5N\15\225B\213l\232\224DI\244"
  "\233\26O\12\225B\233%\363[\262\0P\14\225B\213!\311l\203\22\66\2Q\13\265:\233%\363["
  "\62\246\1R\16\225B\213!\311l\203R\252dZ\0S\14\225B\233%S\325\242\226,\0T\11\225"
  "B\213A\12\373\11U\11\225B\213\314\337\222\5V\12\225B\213\314oI-\2W\13\225B\213\314\227"
  "DIn\1X\16\225B\213LKJIV\211\222\232\26Y\14\225B\213LKJI\26v\2Z\13"
  "\225B\213A\254u\14\7\1[\10\222\302\211\245/\2\134\11\225B\213\260\332\261\0]\10\222\302\11\245"
  "/\3^\6#^\232\6_\6\25>\213A`\6\42\336\211(a\13eB\233\65\31\64-\31\2b"
  "\14\225B\213\260\305\244i\223\242\0c\11eB\233Al\35\2d\13\225B\313\26\323fR\224\0e"
  "\14eB\233%\33\206\60K\26\0f\13\224\302*%\213\206$\353\4g\15\225\66\33\323fR\224P"
  "K\26\0h\12\225B\213\260\305\244\271\5i\7\201B\211d\30j\13\264\266\272\34\310z\223\22\5k"
  "\15\225B\213\260))iIT\311\2l\6\221B\211\7m\15eB\13\245EI\224DI\224\2n"
  "\11eB\213\304\244\271\5o\12eB\233%sK\26\0p\15\225\66\213\304\244i\223\242\204E\0q"
  "\13\225\66\33\323fR\224\260\1r\11eB\213\304$\66\2s\12eB\233%]\265d\1t\12\204"
  "\302\232,\32\222\254Qu\11eB\213\314\223\242\4v\14eB\213LKJI\26F\0w\16eB"
  "\213$Q\22%Q\22\245\13\0x\13eB\213,\251\205YR\13y\14\225\66\213\314\223\242\204Z\262"
  "\0z\12eB\213A\314\332\6\1{\21\227B\234,\321\226\245\247\310\242&Y\222,\1|\6\221B"
  "\213\7}\17\266\66\273\70\31\306:\26\206\303\22g\0~\21\226\302\233!\31\206P\34\36\24e\30\222"
  "(\1\16\226\302\213$\314\224(\315\222,\351?\200\24\231B\255AK\223L\222B)\224BMJ"
  "\322l\220\0\201\24\231>\255AK\223,\323\62-\323\26\35H\322l\220\0\202\22\231>\335t\320*"
  "\311\30\245Q\270\324\262AM\1\0\0\0";

static const uint8_t NatRailClockSmall7[137] U8G2_FONT_SECTION("NatRailClockSmall7") =
  "\12\0\3\3\3\3\3\1\5\7\7\0\0\7\0\7\0\0\0\0\0\0p\60\12?\343Td\274I*"
  "\0\61\10\274\343HF\272\20\62\13?\343TdRIE*=\63\14?\343TdR\331\230&\251\0"
  "\64\14?\343\315H\22%\311Q*\1\65\13?c\34\244f)MR\1\66\14?\343TdT\213\214"
  "&\251\0\67\11?c\134\205Z\325\0\70\15?\343Td\64IEF\223T\0\71\14?\343Td\64"
  "\211\225&\251\0\0\0\0";

static const uint8_t NatRailClockLarge9[177] U8G2_FONT_SECTION("NatRailClockLarge9") =
  "\13\0\4\3\4\4\3\2\5\11\11\0\0\11\0\11\0\0\0\0\0\0\230\60\13\231T\307\205\24\277\222"
  "\270\0\61\11\224W\207\304\210\276 \62\16\231T\307\205\224\234\212\13\71u\7\2\63\17\231T\307\205\224"
  "\234\232B\71*\211\13\0\64\23\231T\327\24\221\204\214\210\32\11!\211\3\61\71\11\0\65\20\231T\307"
  "\301\234\334A\240\34\25\225\304\5\0\66\20\231T\307\205\24\235\334A\204\24+\211\13\0\67\14\231T\303"
  "\201\234\252Ur\32\1\70\17\231T\307\205\24+\211\13)V\22\27\0\71\20\231T\307\205\24+\211\203"
  "\70\71*\211\13\0:\7r\235\2\31\1\0\0\0";

static const uint8_t Underground10[1335] U8G2_FONT_SECTION("Underground10") =
  "\221\0\3\2\3\4\4\5\5\11\12\0\377\11\377\11\0\1^\2\310\5\36 \5\0\314\25!\7I\204"
  "\22\207$\42\7\23\274\24\211\22#\21M\204\66\245$\31\224R\22%\311\240\224\222\4$\17M\204V"
  "\331RJ\262-\211\222d\213\0%\12M\204\226\323\224u:\2&\17F\204\67Z\324\246E\211\226D"
  "I\42\5'\10\42\254\23C\242\0(\10J\204\63J\237\2)\11J\204\23Q\322\213\2*\14=\214"
  "V\225\312AY\232\42\0+\12-\224Va\64Ha\4,\10\42|\23C\242\0-\7\15\244\26\203"
  "\0.\7\22\204\23C\0/\10?\214\327i\237\1\60\14N\204\67C\22\372\61\31\22\0\61\7J\205"
  "\67K?\62\14N\204\67C\22\246\305\216\303\0\63\16N\204\67C\22\246\245\71\25\223!\1\64\16N"
  "\204\227\241\226D\225,\31\306\264\2\65\16N\204\27\207\64\35\344\64\25\223!\1\66\17N\204\67C\22"
  "\252\351\240\204\306dH\0\67\12N\204\27\327b\257)\0\70\17N\204\67C\22\32\223!\11\215\311\220"
  "\0\71\17N\204\67C\22\32\223AM\305dH\0:\10\62\214\23C\70\4;\11:\204\23C\250("
  "\0<\10<\214uQc\3=\10\34\234\25C\70\4>\11<\214\25aS\33\0?\15M\204\66K"
  "\246\205Y\61\207\42\0@\16M\204\66KfI\224D\31\302t\1A\14N\204\67C\22\32\207At"
  "\14B\16N\204\27\203\22\32\207%\64\16\13\0C\14N\204\67C\22\252=&C\2D\13N\204\27"
  "\203\22\372qX\0E\14N\204\27\207\264:\14iu\30F\14N\204\27\207\264:\14i+\0G\16"
  "N\204\67C\22\252\225A\64&C\2H\13N\204\27\241\343\60\210\216\1I\11K\204\24K\324\227\1"
  "J\12M\204\226=jZ\262\0K\20N\204\27\241\226D\225LL\262\250\226\204\1L\11N\204\27i"
  "\277\16\3M\16O\204\30\351\266T\244H\212T\327\0N\15N\204\27\241\270)\221\224h\243\61O\14"
  "N\204\67C\22\372\61\31\22\0P\14N\204\27\203\22\32\207%m\5Q\14V|\67C\22\372)I"
  "\206\70R\16N\204\27\203\22\32\207\245\26\325\222\60S\17N\204\67C\22\252\361\20\247b\62$\0T"
  "\12O\204\30\207,\356\67\0U\12N\204\27\241?&C\2V\14O\204\30\251\257IVI\63\0W"
  "\16O\204\30\251\247H\212\244H\351\226\0X\16O\204\30\251\232d\225\264\222UR\65Y\14O\204\30"
  "i\222U\322\270\67\0Z\13O\204\30\207\70\355\363\60\4[\10J\204\23K_\4\134\11M\204\26a"
  "\265c\1]\10J\204\23J_\6^\6\23\274\64\15_\7\15|\26\203\0`\7\42\254\23\203\24a"
  "\13\65\214\66k\62hZ\62\4b\14E\214\26a\70$\231\333\240\0c\12\65\214\66K&\326\222\5"
  "d\13E\214\226\225AsK\206\0e\14\65\214\66K\66\14a\226,\0f\13L\204UJ\26MY"
  "'\0g\14E|\66K\346\226\14a\262\0h\13M\204\26a\70$\231o\1i\7A\214\22\311\60"
  "j\11S|Ti\324\323\2k\15E\214\26a))iIT\311\2l\10C\214\24Q\337\4m\16"
  "\67\214\30\213\22ER$ER$\25n\11\65\214\26C\222y\13o\12\65\214\66K\346\226,\0p"
  "\14E|\26C\222\271\15J\30\2q\13E|\66K\346\226\14a\1r\12\64\214\25\311\20em\0"
  "s\12\65\214\66K\272j\311\2t\13D\214\65Y\64$Y\243\0u\11\65\214\26\231\267d\10v\12"
  "\65\214\26\231[R\213\0w\13\67\214\30\251S\244tK\0x\13\65\214\26YR\13\263\244\26y\13"
  "E|\26\231\267d\10\7\5z\12\65\214\26\203V\314\262A{\13K\204TIT\311\242Z\0|\6"
  "I\204\26\17}\13K\204\24YTK\242J\4~\21N\204\67C\62\14\241\70<(\312\60$Q\2"
  "\16N\204\27I\230)Q\232%Y\322\200\5\0\204\20\201\10\25\204\26I\242\24\202\5\0\204\20"
  "\203\5\0\204\20\204\5\0\204\20\205\5\0\204\20\206\5\0\204\20\207\5\0\204\20\210\5\0\204\20\211\5"
  "\0\204\20\212\5\0\204\20\213\5\0\204\20\214\5\0\204\20\215\5\0\204\20\216\5\0\204\20\217\5\0\204"
  "\20\220\6\33\237\37\17\221\5\0\204\20\222\5\0\204\20\223\5\0\204\20\224\5\0\204\20\225\5\0\204\20"
  "\226\5\0\204\20\227\5\0\204\20\230\5\0\204\20\231\5\0\204\20\232\5\0\204\20\233\5\0\204\20\234\5"
  "\0\204\20\235\5\0\204\20\236\5\0\204\20\237\5\0\204\20\240\5\0\204\20\241\5\0\204\20\242\5\0\204"
  "\20\243\14=\214VR%\33\242\60\33\4\244\5\0\204\20\245\5\0\204\20\246\5\0\204\20\247\5\0\204"
  "\20\250\5\0\204\20\251\5\0\204\20\252\5\0\204\20\253\5\0\204\20\254\5\0\204\20\255\5\0\204\20\256"
  "\5\0\204\20\257\5\0\204\20\260\12-\244\66KfK\26\0\0\0\0";

static const uint8_t UndergroundClock8[150] U8G2_FONT_SECTION("UndergroundClock8") =
  "\13\0\3\3\3\4\2\2\5\7\10\0\0\10\0\10\0\0\0\0\0\0}\60\12G\305\251\310\370&\251"
  "\0\61\10\304\305\222\234\364\0\62\15G\305\251\310\244B\331L(;\10\63\15G\305\251\310\244\42\62\215"
  "&\251\0\64\15G\305\24\316H\22%\311Q*\1\65\14G\305\70\310\250f\32MR\1\66\14G\305"
  "\251\310\250\26\31\233\244\2\67\12G\305\70\310\204z\225\2\70\15G\305\251\310h\222\212\214MR\1\71"
  "\15G\305\251\310h\22+\215&\251\0:\6\262\257 \22\0\0\0";

// Body font used for plain setup/notification screen text (no icon glyphs), per the configurable font style
static const uint8_t *bodyFont() {
  return NatRailSmall9;
}

static void setRailDetailFont() {
#if defined(DISPLAY_CYD)
  u8g2.setFont(u8g2_font_7x14B_tf);
#else
  u8g2.setFont(NatRailSmall9);
#endif
}

// Service attribution texts
static const char nrAttributionn[] = "Powered by National Rail Enquiries";
static const char rdgAttribution[] = "Powered by Rail Delivery Group";
static const char btAttribution[] = "Powered by bustimes.org";

#define SCREENSAVERINTERVAL 8000      // How often the screen is changed in sleep mode (ms - 8 seconds)
#define DATAUPDATEINTERVAL 90000      // How often we fetch data from National Rail (ms - 1.5 mins) - "default" option
#define FASTDATAUPDATEINTERVAL 45000  // How often we fetch data from National Rail (ms - 45 secs) - "fast" option
#define UGDATAUPDATEINTERVAL 30000    // How often we fetch data from TfL (ms - 30 secs)
#define BUSDATAUPDATEINTERVAL 45000   // How often we fetch data from bustimes.org (ms - 45 secs)
#define RSSUPDATEINTERVAL 600000      // How often to refresh the RSS feed (ms - 10 mins)
#define WEATHERUPDATEINTERVAL 1200000 // How often to update the weather forecast (ms - 20 mins)

// Reusable data transfer structures
rdiStation xfrStation;
stnMessages xfrMessages;
busTubeStation xfrBusTubeStation;
sharedBufferSpace jsonKeyBuffer;

// Station Data (shared)
rdStation station;
// Station Messages (shared)
stnMessages messages;

// Data transfer clients
rdmRailClient rdmRailData(&xfrStation,&xfrMessages,&jsonKeyBuffer);
raildataXmlClient darwinRailData(&xfrStation,&xfrMessages,&jsonKeyBuffer);
TfLdataClient tfldata(&xfrBusTubeStation,&xfrMessages,&jsonKeyBuffer);
busDataClient busdata(&xfrBusTubeStation,&jsonKeyBuffer);
weatherClient currentWeather(&jsonKeyBuffer);
rssClient rss(&jsonKeyBuffer);
github ghUpdate(&jsonKeyBuffer);

static char weatherMsg[MAXWEATHERSIZE];

// Bit and bobs
static unsigned long timer = 0;
static bool isSleeping = false;            // Is the screen sleeping (showing the "screensaver")
static bool sleepEnabled = false;          // Is overnight sleep enabled?
static bool forcedSleep = false;           // Is the system in manual sleep mode?
static bool forcedAwake = false;           // Was the system woken by touch sensor?
static int stayAwakeSeconds = 300;         // How long to force stay awake since last tap
static bool sleepClock = true;             // Showing the clock in sleep mode?
static bool useNSEclockForSleep = false;   // Use the large NSE clock in "sleep" mode?
static bool longPressClock = false;        // Long press switches to NSE clock mode
static bool showClockNoServices = false;   // Show the NSE clock when no train services at location
static bool noServiceClockIsActive = false;// NSE clock is active due to no services at location
static bool NSEclockIsActive = false;      // Is the large NSE clock active
static bool softResetNeeded = false;       // Is a soft reset pending?
static bool manualUpdateCheck = false;     // Has the GUI requested a firmware update check
static bool showDataIcon = false;          // Show the data transfer indicator?
static bool updateIconVisible = false;     // Is the data update icon visible?
static bool dateEnabled = false;           // Showing the date on screen?
static bool weatherEnabled = false;        // Showing weather at station location. Requires an OpenWeatherMap API key.
static bool enableBus = false;             // Include Bus services on the board?
static bool firmwareUpdates = true;        // Check for and install firmware updates automatically at boot?
static bool dailyUpdateCheck = false;      // Check for and install firmware updates at midnight?
static byte sleepStarts = 0;               // Hour at which the overnight sleep (screensaver) begins
static byte sleepEnds = 6;                 // Hour at which the overnight sleep (screensaver) ends
static int brightness = 50;                // Initial backlight brightness level
static unsigned long lastWiFiReconnect=0;  // Last WiFi reconnection time (millis)
static bool firstLoad = true;              // Are we loading for the first time (no station config)?
static int prevProgressBarPosition=0;      // Used for progress bar smooth animation
static int startupProgressPercent;         // Initialisation progress
static bool wifiConnected = false;         // Connected to WiFi?
volatile unsigned long nextDataUpdate = 0; // Next National Rail update time (millis)
static int dataLoadSuccess = 0;            // Count of successful data downloads
static int dataLoadFailure = 0;            // Count of failed data downloads
static unsigned long lastLoadFailure = 0;  // When the last failure occurred
static bool noDataLoaded = true;           // True if no data received for the location
static unsigned long lastDataLoadTime = 0; // Timestamp of last data load
static long apiRefreshRate = DATAUPDATEINTERVAL; // User selected refresh rate for National Rail API (90/45 secs)
static int dateWidth;                      // Width of the displayed date in pixels
static int dateDay;                        // Day of the month of displayed date
static bool noScrolling = false;           // Suppress all horizontal scrolling
static bool flipScreen = false;            // Rotate screen 180deg
static String timezone = "";               // custom (non UK) timezone for the clock
static bool hidePlatform = false;          // Hide platform numbers on Rail board?
static bool hideOrdinals = false;          // Hide service ordinals (2nd, 3rd, 4th etc.)
static bool showLastSeen = false;          // Include last reported arrival after the calling at list
static bool showFullCalling = true;        // Wait for the "Calling at" list to finish scrolling before changing the primary service
static bool showFullMsgs = true;           // Wait for the current service message or RSS feed to finish scrolling before changing primary service
static bool showServiceMsgs = true;        // Show station and service messages (rail/tube)
static bool showTubeCurrentLocation=false; // Show the current location of the primary tube service
static int nrTimeOffset = 0;               // Offset minutes for Rail departures display
static int prevUpdateCheckDay;             // Day of the month the last daily firmware update check was made
static unsigned long fwUpdateCheckTimer=0; // Next time to check if the day has rolled over for firmware update check
static bool apiKeys = false;               // Does apikeys.json exist?
#if defined(DISPLAY_CYD)
static bool touchEnabled = true;            // Onboard BOOT button
#else
static bool touchEnabled = false;           // Optional TTP223 touch sensor installed?
#endif
static bool useRDMclient = false;          // Use the new Rail Data Marketplace API instead of Darwin Lite
static bool enableScheduler = false;
static bool enableCarousel = false;
static int numCarouselSlots = 0;
static int currentCarouselSlot = 0;
static int numScheduleSlots = 0;
static int currentScheduleSlot = 0;
static unsigned long nextSchedulerCheck = 0;
static char hostname[33];                  // Network hostname (mDNS)
static char myUrl[24];                     // Stores the board's own url
static char webPassword[33] = "";          // Web interface authentication password (empty = disabled)
static char webUsername[33] = "admin";     // Web interface authentication username

// WiFi Manager status
static bool wifiConfigured = false;        // Has WiFi Manager used the captive portal

// Station Board Data
static char locationCode[13];              // CRS, Naptan or Atco code of active location
static char locationName[MAXLOCATIONSIZE]; // Station/Bus stop long name
static char locationFilter[MAXFILTERSIZE];
static char locationCleanFilter[MAXFILTERSIZE];
static float locationLat=0;
static float locationLon=0;
static bool railIsSet = false;
static bool tubeIsSet = false;
static bool busIsSet = false;
static bool schedulerActive = false;
static bool carouselActive = false;
static int activeSlotEventTime;
static int nextSlotEventTime;

static char nrToken[37] = "";              // National Rail Darwin Lite Tokens are in the format nnnnnnnn-nnnn-nnnn-nnnn-nnnnnnnnnnnn, where each 'n' represents a hexadecimal character (0-9 or a-f).
static String rdmDeparturesApiKey = "";    // RDM Consumer key for DeparturesBoard API
static String rdmServiceApiKey = "";       // RDM Consumer key for ServiceDetails API
static char tflAppKey[33] = "";            // TfL app_key (not usually needed)
static char callingCrsCode[4] = "";        // Station code to filter routes on
static char callingStation[45] = "";       // Calling filter station friendly name
static char lineId[33];                    // Underground line to filter on
static char lineDirection[9];              // Underground direction filter
static int busDestX;                       // Variable margin for bus destination

enum boardModes {
  MODE_LOADCONFIG = -1,
  MODE_NEXTMODE = -2,
  MODE_NEXTSCHEDULE = -3,
  MODE_RAIL = 0,
  MODE_TUBE = 1,
  MODE_BUS = 2
};
boardModes boardMode = MODE_RAIL;

// National Rail entry point
#define MAXHOSTSIZE 48                     // Maximum size of the wsdl Host
#define MAXAPIURLSIZE 48                   // Maximum size of the wsdl url
static char wsdlHost[MAXHOSTSIZE];         // wsdl Host name
static char wsdlAPI[MAXAPIURLSIZE];        // wsdl API url

// Coach class availability
static const char firstClassSeating[] = " First class seating only.";
static const char standardClassSeating[] = " Standard class seating only.";
static const char dualClassSeating[] = " First and Standard class seating available.";

// Animation
#define frameTimeRail 25
#define frameTimeTube 18
#define frameTimeBus 40
static int numMessages=0;
static int scrollStopsXpos = 0;
static int scrollStopsYpos = 0;
static int scrollStopsLength = 0;
static bool isScrollingStops = false;
static bool isShowingCalling = false;
static int currentMessage = 0;
static int prevMessage = 0;
static int prevScrollStopsLength = 0;
static long delayMs;
static char line2[5+MAXBOARDMESSAGES][MAXCALLINGSIZE+12];
static int msgLine;
static int msgWidth;
static int msgMargin;

// Line 3 (additional services)
static int line3Service = 0;
static int scrollServiceYpos = 0;
static bool isScrollingService = false;
static int prevService = 0;
static bool isShowingVia=false;
static unsigned long serviceTimer=0;
static unsigned long viaTimer=0;
static bool showingMessage = false;
#if defined(DISPLAY_CYD)
static const int CYD_PRIMARY_SERVICE_SCROLL_DISTANCE = PRIMARY_MESSAGE_LINE - LINE1;
static const uint8_t CYD_TILE_PRIMARY_SERVICE_Y = 8;
static const uint8_t CYD_TILE_PRIMARY_SERVICE_H = 5;
static const int CYD_SECOND_SERVICE_INFO_OFFSET = 22;
static const int CYD_DETAIL_STATUS_RIGHT = SCREEN_WIDTH - 12;
static const uint8_t CYD_TILE_SERVICE_PANEL_Y = 8;     // y = 64..175 (14 tile rows)
static const uint8_t CYD_TILE_SERVICE_PANEL_H = 14;
static const uint8_t CYD_TILE_PRIMARY_MSG_Y = 13;     // y = 104..127 (3 tile rows)
static const uint8_t CYD_TILE_PRIMARY_MSG_H = 3;
static const uint8_t CYD_TILE_SECONDARY_SERVICE_Y = 16;// y = 128..175 (6 tile rows)
static const uint8_t CYD_TILE_SECONDARY_SERVICE_H = 6;
static const uint8_t CYD_TILE_BOTTOM_TICKER_Y = 21;   // y = 168..199 (4 tile rows)
static const uint8_t CYD_TILE_BOTTOM_TICKER_H = 4;
static const uint8_t CYD_TILE_CLOCK_Y = 25;           // y = 200..239 (5 tile rows)
static const uint8_t CYD_TILE_CLOCK_H = 5;
static char displayedPrimaryServiceMessage[MAXCALLINGSIZE+12];
static int primaryServiceMessageScrollX = 0;
static int primaryServiceMessageWidth = 0;
static char cydPrimaryMessages[5+MAXBOARDMESSAGES][MAXCALLINGSIZE+12];
static int cydPrimaryMessageCount = 0;
static int cydPrimaryMessageIndex = 0;
static int cydSecondaryServiceIndex = 1;
static unsigned long primaryServiceMessageTimer = 0;
static rdService cydPreviousPrimaryService;
static bool cydPreviousPrimaryPlatformAvailable = false;
static bool cydPreviousPrimaryShowVia = false;
static bool cydPrimaryServiceHasPrevious = false;
static bool isScrollingCydPrimaryService = false;
static int cydPrimaryServiceScrollY = 0;
#endif

// TfL/bus specific animation
static int scrollPrimaryYpos = 0;
static bool isScrollingPrimary = false;
static bool attributionScrolled = false;

static char displayedTime[9] = "";        // The currently displayed time
static char currentTime[9] = "";          // The current time (keep updated in loop)
static unsigned long lastTimeUpdate = 0;
static unsigned long refreshTimer = 0;

// Weather Stuff
static unsigned long nextWeatherUpdate = 0;            // When the next weather update is due
static char openWeatherMapApiKey[33] = "";             // If no OWM API key is provided, we use Open-Meteo weather data

// RSS Client
static bool rssEnabled = false;                        // Add RSS feed to the messages
static bool rssPriority = false;                       // Prioritise RSS feed
static unsigned long nextRssUpdate = 0;                // When the next RSS update is due
static String rssURL;                                  // RSS URL to use
static String rssName;                                 // Name of feed for atrribution
static char rssMessage[MAXMESSAGESIZE] = "";           // Holds the current, formatted, RSS message


#if defined(DISPLAY_CYD)
// CYD touchscreen and BOOT button
touchSensor button(0);
#else
// Optional TTP223 touch sensor for the OLED board
touchSensor button(GPIO_NUM_34);
#endif

// FreeRTOS Task Handle and Status Flags
TaskHandle_t fetchTaskHandle = NULL;
volatile bool fetchComplete = false;
volatile bool fetchInProgress = false;
volatile bool rssFetchComplete = false;
volatile bool weatherFetchComplete = false;
volatile int lastUpdateResult = UPD_SUCCESS;
volatile int lastWeatherUpdateResult = UPD_SUCCESS;
volatile int lastRssUpdateResult = UPD_SUCCESS;

enum fetchModes {
  FETCH_BOARD = 0,
  FETCH_WEATHER = 1,
  FETCH_RSS = 2
};
fetchModes fetchMode = FETCH_BOARD;

/*
 * Graphics helper functions for the virtual departures-board display
*/
void blankArea(int x, int y, int w, int h) {
#if defined(DISPLAY_CYD)
  if (w == 256) w = SCREEN_WIDTH;
  if (h >= 8 && h <= 11) h = 20;
#endif
  u8g2.setDrawColor(0);
  u8g2.drawBox(x,y,w,h);
  u8g2.setDrawColor(1);
}

void setDisplayClipWindow(int x0, int y0, int x1, int y1) {
#if defined(DISPLAY_CYD)
  if (x1 == 256) x1 = SCREEN_WIDTH;
  if (y1 - y0 >= 8 && y1 - y0 <= 11) y1 = y0 + 20;
#endif
  u8g2.setClipWindow(x0,y0,x1,y1);
}

int getStringWidth(const char *message) {
  return u8g2.getStrWidth(message);
}

int railDetailBaseline(int rowTop) {
#if defined(DISPLAY_CYD)
  return rowTop + u8g2.getAscent();
#else
  return rowTop - 1;
#endif
}

int railDetailScrollBaseline(int legacyBaseline) {
#if defined(DISPLAY_CYD)
  return legacyBaseline + u8g2.getAscent();
#else
  return legacyBaseline;
#endif
}

void drawTruncatedText(const char *message, int line, int x) {
  char buff[strlen(message)+4];
  int maxWidth = SCREEN_WIDTH - 6 - x;
  strcpy(buff,message);
  int i = strlen(buff);
  while (u8g2.getStrWidth(buff)>maxWidth && i) buff[i--] = '\0';
  strcat(buff,"...");
  u8g2.drawStr(x,line,buff);
}

void centreText(const char *message, int line, int margin=0, int maxWidth = SCREEN_WIDTH) {
  int width = u8g2.getStrWidth(message);
  if (width<=maxWidth) u8g2.drawStr(((maxWidth-width)/2)+margin,line,message);
  else drawTruncatedText(message,line,0);
}

void drawStationTitle(const char *message, int x, int y) {
  u8g2.drawStr(x,y,message);
#if defined(DISPLAY_CYD)
  u8g2.drawStr(x+1,y,message);
#endif
}

void drawTruncatedStationTitle(const char *message, int line, int x, int maxWidth = -1) {
  char buff[strlen(message)+4];
  if (maxWidth < 0) maxWidth = SCREEN_WIDTH - 6 - x;
  strcpy(buff,message);
  int i = strlen(buff);
#if defined(DISPLAY_CYD)
  int dotsWidth = u8g2.getStrWidth("...");
  int targetWidth = (maxWidth > dotsWidth) ? (maxWidth - dotsWidth) : 0;
  while (u8g2.getStrWidth(buff)>targetWidth && i) buff[i--] = '\0';
  if (strlen(buff) > 0 && buff[strlen(buff)-1] == ' ') buff[strlen(buff)-1] = '\0';
#else
  while (u8g2.getStrWidth(buff)>maxWidth && i) buff[i--] = '\0';
#endif
  strcat(buff,"...");
  drawStationTitle(buff,x,line);
}

void centreStationTitle(const char *message, int line, int margin=0, int maxWidth = SCREEN_WIDTH) {
  int width = u8g2.getStrWidth(message);
  if (width<=maxWidth) drawStationTitle(message,((maxWidth-width)/2)+margin,line);
  else drawTruncatedStationTitle(message,line,margin,maxWidth);
}

void drawProgressBar(int percent) {
#if defined(DISPLAY_CYD)
  const int x = 32;
  const int y = 80;
  const int width = 240;
  const int height = 14;
#else
  const int x = 32;
  const int y = 36;
  const int width = 192;
  const int height = 12;
#endif
  int newPosition = (percent*(width-2))/100;
  u8g2.drawFrame(x,y,width,height);
  if (prevProgressBarPosition>newPosition) {
    for (int i=prevProgressBarPosition;i>=newPosition;i--) {
      u8g2.setDrawColor(0);
      u8g2.drawBox(x+1,y+1,width-2,height-2);
      u8g2.setDrawColor(1);
      u8g2.drawBox(x+1,y+1,i,height-2);
      u8g2.updateDisplayArea(0,3,32,3);
      delay(5);
    }
  } else {
    for (int i=prevProgressBarPosition;i<=newPosition;i++) {
      u8g2.setDrawColor(0);
      u8g2.drawBox(x+1,y+1,width-2,height-2);
      u8g2.setDrawColor(1);
      u8g2.drawBox(x+1,y+1,i,height-2);
      u8g2.updateDisplayArea(0,3,32,3);
      delay(5);
    }
  }
  prevProgressBarPosition=newPosition;
}

void progressBar(const char *text, int percent) {
  u8g2.setFont(bodyFont());
#if defined(DISPLAY_CYD)
  blankArea(0,52,SCREEN_WIDTH,48);
  centreText(text,52);
#else
  blankArea(0,24,256,25);
  centreText(text,24);
#endif
  drawProgressBar(percent);
}

void drawFirmware() {
  char firmware[16];
  sprintf(firmware,"B%d.%d-W%d.%d",VERSION_MAJOR,VERSION_MINOR,WEBAPPVER_MAJOR,WEBAPPVER_MINOR);
#if defined(DISPLAY_CYD)
  u8g2.drawStr(0,SCREEN_HEIGHT-20,firmware);
#else
  u8g2.drawStr(0,53,firmware);
#endif
}

void drawStartupHeading() {
#if defined(DISPLAY_CYD)
  u8g2.setTextScale(1);
  u8g2.setFontPosTop();
  u8g2.setFont(NatRailTall12);
  centreText("Departures Board",20);
  u8g2.setFont(bodyFont());
  drawFirmware();
#else
  u8g2.setFont(NatRailTall12);
  centreText("Departures Board",0);
  u8g2.setFont(bodyFont());
  drawFirmware();
#endif
}

void drawStationHeader(const char *stopName, const char *callingStopName, const char *platFilter, const int timeOffset) {
#if defined(DISPLAY_CYD)
  uint8_t previousTextScale = u8g2.getTextScale();
  u8g2.setTextScale(2);
#endif

  // Clear the top line
#if defined(DISPLAY_CYD)
  blankArea(0,LINE0,SCREEN_WIDTH,LINE1-1);
#else
  if (boardMode == MODE_TUBE || boardMode == MODE_BUS) {
    blankArea(0,ULINE0,256,ULINE1-1);
  } else {
    blankArea(0,LINE0,256,LINE1-1);
  }
#endif

#if defined(DISPLAY_CYD)
  u8g2.setFont(NatRailTall12);
  const int titleBaseline = 10 + (u8g2.getAscent() * u8g2.getTextScale());
#else
  u8g2.setFont(NatRailSmall9);
  const int titleBaseline = LINE0-1;
#endif
  char boardTitle[95];
  strlcpy(boardTitle,stopName,sizeof(boardTitle));
  if (timeOffset || platFilter[0] || callingStopName[0]) strlcat(boardTitle," ",sizeof(boardTitle));

  if (timeOffset) {
    char offset[9];
    sprintf(offset,"\x8F%+dm ",timeOffset);
    strlcat(boardTitle,offset,sizeof(boardTitle));
  }
  if (platFilter[0]) {
    strlcat(boardTitle,(boardMode == MODE_BUS)?"\x8E":"\x8D",sizeof(boardTitle));
    strlcat(boardTitle,platFilter,sizeof(boardTitle));
    strlcat(boardTitle," ",sizeof(boardTitle));
  }
  if (callingStopName[0]) {
    strlcat(boardTitle,"(\x81",sizeof(boardTitle));
    strlcat(boardTitle,callingStopName,sizeof(boardTitle));
    strlcat(boardTitle,")",sizeof(boardTitle));
  }

  int titleOffset = 0;
  if (schedulerActive || carouselActive) titleOffset = 13;
  int boardTitleWidth = getStringWidth(boardTitle);

  if (dateEnabled) {
    // Get the date
    char sysTime[29];
    strftime(sysTime,29,"%a %d %b",&timeinfo);
    dateWidth = getStringWidth(sysTime);
    dateDay = timeinfo.tm_mday;
#if defined(DISPLAY_CYD)
    int const effDateWidth = dateWidth + 1; // Account for x+1 double-strike bolding in drawStationTitle
    int const dateX = SCREEN_WIDTH - effDateWidth;
    drawStationTitle(sysTime,dateX,titleBaseline); // Right-aligned date top in title font

    int const availableTitleWidth = dateX - 10 - titleOffset;
    if (boardTitleWidth <= availableTitleWidth) {
      if ((SCREEN_WIDTH - boardTitleWidth) / 2 < effDateWidth + 8) {
        // Station name left aligned
        drawStationTitle(boardTitle,titleOffset,titleBaseline);
      } else {
        centreStationTitle(boardTitle,titleBaseline);
      }
    } else {
      drawTruncatedStationTitle(boardTitle,titleBaseline,titleOffset,availableTitleWidth);
    }
#else
    int const dateY=55;
    if (callingStopName[0] || boardTitleWidth+dateWidth+10+titleOffset>=SCREEN_WIDTH) {
      blankArea(SCREEN_WIDTH-70,dateY,70,SCREEN_HEIGHT-dateY);
      u8g2.drawStr(SCREEN_WIDTH-dateWidth,dateY-1,sysTime); // Date bottom right
      if (boardTitleWidth+titleOffset < SCREEN_WIDTH) centreStationTitle(boardTitle,titleBaseline);
      else drawTruncatedStationTitle(boardTitle,titleBaseline,titleOffset);
    } else {
      u8g2.drawStr(SCREEN_WIDTH-dateWidth,titleBaseline,sysTime); // right-aligned date top
      if ((SCREEN_WIDTH-boardTitleWidth)/2 < dateWidth+8) {
        // station name left aligned
        drawStationTitle(boardTitle,titleOffset,titleBaseline);
      } else {
        centreStationTitle(boardTitle,titleBaseline);
      }
    }
#endif
  } else {
    if (boardTitleWidth+titleOffset < SCREEN_WIDTH) centreStationTitle(boardTitle,titleBaseline);
    else drawTruncatedStationTitle(boardTitle,titleBaseline,titleOffset);
  }

  if (titleOffset) u8g2.drawStr(0,titleBaseline,schedulerActive?"\x87":"\x88");
#if defined(DISPLAY_CYD)
  u8g2.setTextScale(previousTextScale);
#endif
}

// Draw a 7-segment digit at x,y with height h
void draw7Segment8(int x, int y, int h, char digit) {
    int w = (h * 7) / 10;    // Width is 70% of height
    int t = (h * 15) / 100;  // Thickness of the segments

    // Calculate uniform gap sizing between segments
    int g = (h >= 40) ? (h / 40) : 1;
    int half_h = h / 2;
    int gh1 = g / 2;
    int gh2 = g - gh1;

    // Helper lambda to draw a filled quadrilateral using two triangles.
    auto drawQuad = [&](int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3) {
        u8g2.drawTriangle(x0, y0, x1, y1, x2, y2);
        u8g2.drawTriangle(x0, y0, x2, y2, x3, y3);
    };

    // Top Segment
    if (strchr("02356789",digit) != nullptr)
        drawQuad(x + g, y,
             x + w - g, y,
             x + w - t - g, y + t,
             x + t + g, y + t);

    // Top-Right Segment
    if (strchr("01234789",digit) != nullptr)
      drawQuad(x + w, y + g,
             x + w, y + half_h - gh1,
             x + w - t, y + half_h - gh1,
             x + w - t, y + t + g);

    // Bottom-Right Segment
    if (strchr("013456789",digit) != nullptr)
      drawQuad(x + w, y + half_h + gh2,
             x + w, y + h - g,
             x + w - t, y + h - t - g,
             x + w - t, y + half_h + gh2);

    // Bottom Segment
    if (strchr("0235689",digit) != nullptr)
      drawQuad(x + g, y + h,
             x + w - g, y + h,
             x + w - t - g, y + h - t,
             x + t + g, y + h - t);

    // Bottom-Left Segment
    if (strchr("0268",digit) != nullptr)
      drawQuad(x, y + half_h + gh2,
             x + t, y + half_h + gh2,
             x + t, y + h - t - g,
             x, y + h - g);

    // Top-Left Segment
    if (strchr("045689",digit) != nullptr)
      drawQuad(x, y + g,
             x + t, y + t + g,
             x + t, y + half_h - gh1,
             x, y + half_h - gh1);

    // Middle Segment
    if (strchr("2345689",digit) != nullptr)
      drawQuad(x + t + g, y + half_h - t / 2,
             x + w - t - g, y + half_h - t / 2,
             x + w - t - g, y + half_h + t / 2,
             x + t + g, y + half_h + t / 2);
}

// Draws the full screen, Network SouthEast style clock
void drawNSEclock(bool fullDraw = true) {
  char clockdigits[7];
  sprintf(clockdigits,"%02d%02d%02d",timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec);

#if defined(DISPLAY_CYD)
  const int digitH = 50;
  const int secH = 36;
  const int top = (SCREEN_HEIGHT - digitH) / 2; // 95

  if (fullDraw) {
    u8g2.clearBuffer();
  } else {
    blankArea(8, top - 2, 304, digitH + 4);
  }
  draw7Segment8(10, top, digitH, clockdigits[0]);
  draw7Segment8(61, top, digitH, clockdigits[1]);
  u8g2.drawFilledEllipse(114, top + 12, 4, 4);
  u8g2.drawFilledEllipse(114, top + 38, 4, 4);
  draw7Segment8(132, top, digitH, clockdigits[2]);
  draw7Segment8(183, top, digitH, clockdigits[3]);
  u8g2.drawFilledEllipse(236, top + 25, 4, 4);
  draw7Segment8(252, top + (digitH - secH), secH, clockdigits[4]);
  draw7Segment8(285, top + (digitH - secH), secH, clockdigits[5]);

  if (fullDraw) u8g2.sendBuffer();
  else u8g2.updateDisplayArea(0, 11, CYD_NATIVE_TILE_WIDTH, 8);
#else
  int top;
  if (fullDraw) {
    u8g2.clearBuffer();
    top = 11;
  } else {
    top = 6;
    blankArea(8,top,248,41);
  }
  draw7Segment8(8,top,41,clockdigits[0]);
  draw7Segment8(50,top,41,clockdigits[1]);
  draw7Segment8(109,top,41,clockdigits[2]);
  draw7Segment8(151,top,41,clockdigits[3]);
  draw7Segment8(202,top+11,30,clockdigits[4]);
  draw7Segment8(229,top+11,30,clockdigits[5]);
  u8g2.drawFilledEllipse(190,top+21,4,4);
  u8g2.drawFilledEllipse(93,top+10,4,4);
  u8g2.drawFilledEllipse(93,top+30,4,4);

  if (fullDraw) u8g2.sendBuffer(); else u8g2.updateDisplayArea(1,0,31,6);
#endif
}

// Draw the NR clock (if the time has changed)
void drawCurrentTime() {
#if !defined(DISPLAY_CYD)
  char timeSeg[7];
#endif

  if (strcmp(displayedTime,currentTime)) {
    if (noServiceClockIsActive) drawNSEclock(false);
    else {
#if defined(DISPLAY_CYD)
      u8g2.setFont(u8g2_font_logisoso20_tn);
      int clockWidth = u8g2.getStrWidthUnscaled("88:88:88");
      int clockX = (SCREEN_WIDTH-clockWidth)/2;
      int clockHeight = u8g2.getAscent() - u8g2.getDescent();
      int clockTop = SCREEN_HEIGHT - 10 - clockHeight;
      blankArea(clockX,LINE4,clockWidth,SCREEN_HEIGHT-LINE4);
      u8g2.setFontPosTop();
      u8g2.drawStrUnscaled(clockX,clockTop,currentTime);
      u8g2.setFontPosBaseline();
      setRailDetailFont();
      u8g2.updateDisplayArea(0, CYD_TILE_CLOCK_Y, CYD_NATIVE_TILE_WIDTH, CYD_TILE_CLOCK_H);
#else
      u8g2.setFont(NatRailClockLarge9);
      blankArea(96,LINE4,64,SCREEN_HEIGHT-LINE4);
      strlcpy(timeSeg,currentTime,7);
      u8g2.drawStr(96,LINE4-1,timeSeg);
      u8g2.setFont(NatRailClockSmall7);
      strcpy(timeSeg,currentTime+6);
      u8g2.drawStr(144,LINE4+1,timeSeg);
      u8g2.setFont(NatRailSmall9);
      u8g2.updateDisplayArea(12,6,8,2);
#endif
      strcpy(displayedTime,currentTime);
      if (dateEnabled && timeinfo.tm_mday!=dateDay) {
        // Need to update the date on screen
        if (boardMode == MODE_BUS) drawStationHeader(locationName,"",locationFilter,0);
        else if (boardMode == MODE_TUBE) drawStationHeader(locationName,"","",0);
        else drawStationHeader(station.location,callingStation,locationFilter,nrTimeOffset);
        u8g2.sendBuffer();  // Just refresh on new date
      }
    }
  }
}

// Screensaver Screen
void drawSleepingScreen() {
  char sysTime[8];
  char sysDate[29];

  u8g2.setContrast(DIMMED_BRIGHTNESS);
  u8g2.clearBuffer();
  if (sleepClock) {
    sprintf(sysTime,"%02d:%02d",timeinfo.tm_hour,timeinfo.tm_min);
    strftime(sysDate,29,"%d %B %Y",&timeinfo);

    int offset = (getStringWidth(sysDate)-44)/2;
    u8g2.setFont(NatRailClockLarge9);
    int y = random(39);
    int x = random(SCREEN_WIDTH-getStringWidth(sysDate));
    u8g2.drawStr(x+offset,y,sysTime);
    u8g2.setFont(NatRailSmall9);
    u8g2.drawStr(x,y+13,sysDate);
  }
  u8g2.sendBuffer();
}

void showUpdateIcon(bool show) {
  if (!showDataIcon) return;
  if (show) {
    if (noServiceClockIsActive) {
      u8g2.setFont(NatRailSmall9);
      u8g2.drawStr(0,-2,"\x81");
    } else {
#if defined(DISPLAY_CYD)
      u8g2.setFont(NatRailTall12);
      u8g2.drawStr(10,224,"}");
      setRailDetailFont();
#else
      u8g2.setFont(NatRailTall12);
      u8g2.drawStr(0,50,"}");
      if (boardMode == MODE_TUBE) u8g2.setFont(Underground10);
      else u8g2.setFont(NatRailSmall9);
#endif
    }
    updateIconVisible = true;
  } else {
#if defined(DISPLAY_CYD)
    if (noServiceClockIsActive) blankArea(0,0,6,6); else blankArea(10,LINE4,16,SCREEN_HEIGHT-LINE4);
#else
    if (noServiceClockIsActive) blankArea(0,0,6,6); else blankArea(0,50,6,13);
#endif
    updateIconVisible = false;
  }
#if defined(DISPLAY_CYD)
  if (noServiceClockIsActive) u8g2.updateDisplayArea(0,0,1,1); else u8g2.updateDisplayArea(0,CYD_TILE_CLOCK_Y,4,CYD_TILE_CLOCK_H);
#else
  if (noServiceClockIsActive) u8g2.updateDisplayArea(0,0,1,1); else u8g2.updateDisplayArea(0,6,1,2);
#endif
}

/*
 * Setup / Notification Screen Layouts
*/
void showSetupScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  centreText("Departures Board first-time setup",0);
  u8g2.setFont(bodyFont());
  centreText("To configure Wi-Fi, please connect to the",18);
  centreText("the \"Departures Board\" network and go to",32);
  centreText("http://192.168.4.1 in a web browser.",46);
  u8g2.sendBuffer();
}

void showNoDataScreen() {
  noServiceClockIsActive = false;
  u8g2.clearBuffer();
  char msg[60];
  u8g2.setFont(NatRailTall12);
  switch (boardMode) {
    case MODE_RAIL:
      sprintf(msg,"No data available for station code \"%s\".",locationCode);
      break;
    case MODE_TUBE:
      strcpy(msg,"No data available for the selected station.");
      break;
    case MODE_BUS:
      strcpy(msg,"No data available for the selected bus stop.");
      break;
  }
  centreText(msg,-1);
  u8g2.setFont(bodyFont());
  centreText("Please check you have selected a valid location",14);
  centreText("Go to the URL below to choose a location...",26);
  centreText(myUrl,40);
  u8g2.sendBuffer();
}

void showSetupKeysHelpScreen() {
  u8g2.clearBuffer();
  char msg[60];
  u8g2.setFont(NatRailTall12);
  centreText("Departures Board Setup",-1);
  u8g2.setFont(bodyFont());
  centreText("Next, you need to enter your API keys.",16);
  centreText("Please go to the URL below to start...",28);
  u8g2.setFont(NatRailTall12);
  centreText(myUrl,50);
  u8g2.sendBuffer();
}

void showSetupCrsHelpScreen() {
  u8g2.clearBuffer();
  char msg[60];
  u8g2.setFont(NatRailTall12);
  centreText("Departures Board Setup",-1);
  u8g2.setFont(bodyFont());
  centreText("Next, you need to choose a location. Please",16);
  centreText("go to the URL below to choose a station...",28);
  u8g2.setFont(NatRailTall12);
  centreText(myUrl,50);
  u8g2.sendBuffer();
}

void showWsdlFailureScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  centreText("The National Rail data feed is unavailable.",-1);
  u8g2.setFont(bodyFont());
  centreText("WDSL entry point could not be accessed, so the",14);
  centreText("Departures Board cannot be loaded.",26);
  centreText("Please try again later. :(",40);
  u8g2.sendBuffer();
}

void showTokenErrorScreen() {
  char msg[60];
  noServiceClockIsActive = false;
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  switch (boardMode) {
    case MODE_RAIL:
      if (useRDMclient) {
        centreText("Access to the Rail Delivery Group api denied.",-1);
      } else {
        centreText("Access to the National Rail database denied.",-1);
        strcpy(nrToken,"");
      }
      break;
    case MODE_TUBE:
      centreText("Access to the TfL database denied.",-1);
      break;
    case MODE_BUS:
      centreText("Access to the bustimes database denied.",-1);
      break;
  }
  u8g2.setFont(bodyFont());
  centreText("You must enter a valid api key, please",14);
  centreText("check you have entered it correctly below:",26);
  sprintf(msg,"%s/keys.htm",myUrl);
  centreText(msg,40);
  u8g2.sendBuffer();
}

void showCRSErrorScreen() {
  noServiceClockIsActive = false;
  u8g2.clearBuffer();
  char msg[60];
  u8g2.setFont(NatRailTall12);
  switch (boardMode) {
    case MODE_RAIL:
      sprintf(msg,"The station code \"%s\" is not valid.",locationCode);
      break;
    case MODE_TUBE:
      strcpy(msg,"The Underground station is not valid");
      break;
    case MODE_BUS:
      sprintf(msg,"The atco code \"%s\" is not valid.",locationCode);
      break;
  }
  centreText(msg,-1);
  u8g2.setFont(bodyFont());
  centreText("Please ensure you have selected a valid station.",14);
  centreText("Go to the URL below to choose a station...",26);
  centreText(myUrl,40);
  u8g2.sendBuffer();
}

void showFirmwareUpdateWarningScreen(const char *msg) {
  char countdown[60];
  int x=SCREEN_WIDTH;
  int secs=30;
  unsigned long ticks,tocks;

  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  centreText("Firmware Update Available",-1);
  u8g2.setFont(NatRailSmall9);
  centreText("A new version of the Departures Board firmware",14);
  centreText("* DO NOT REMOVE THE POWER DURING THE UPDATE *",54);

  int msgWidth = getStringWidth(msg);
  if (msgWidth < SCREEN_WIDTH-8) {
    sprintf(countdown,"\"%s\"",msg);
    centreText(countdown,40);
  } else {
    sprintf(rssMessage,"\"%s\"",msg);
    while (strlen(rssMessage) + strlen(msg) + 3 < MAXMESSAGESIZE) {
      strcat(rssMessage,"\x90\"");
      strcat(rssMessage,msg);
      strcat(rssMessage,"\"");
    }
    msgWidth = getStringWidth(rssMessage);
    secs=45;
  }
  while (secs>=0) {
    sprintf(countdown,"will be installed in %d seconds. This provides:",secs);
    blankArea(0,26,SCREEN_WIDTH,10);
    centreText(countdown,26);
    if (msgWidth < SCREEN_WIDTH) {
      u8g2.sendBuffer();
      delay(1000);
      secs--;
    } else {
      ticks = millis()+1000;
      while (millis()<ticks) {
        tocks = millis() + 25;
        blankArea(0,40,SCREEN_WIDTH,10);
        u8g2.drawStr(x,40,rssMessage);
        u8g2.sendBuffer();
        x--;
        if (x < -msgWidth) x=SCREEN_WIDTH;
        while (millis()<tocks) delay(1);
      }
      secs--;
    }
  }
}

void showFirmwareUpdateProgress(int percent) {
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  centreText("Firmware Update in Progress",-1);
  u8g2.setFont(bodyFont());
  progressBar("Updating Firmware",percent);
  centreText("* DO NOT REMOVE THE POWER DURING THE UPDATE *",54);
  u8g2.sendBuffer();
}

void showUpdateCompleteScreen(const char *title, const char *msg1, const char *msg2, const char *msg3, int secs, bool showReboot) {
  char countdown[60];
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  centreText(title,-1);
  u8g2.setFont(bodyFont());
  centreText(msg1,14);
  centreText(msg2,26);
  centreText(msg3,40);
  if (showReboot) sprintf(countdown,"The system will restart in %d seconds.",secs);
  else sprintf(countdown,"The system will continue in %d seconds.",secs);
  centreText(countdown,54);
  u8g2.sendBuffer();
}

void showSwitchScreen() {
  u8g2.clearBuffer();
#if defined(DISPLAY_CYD)
  u8g2.setTextScale(1);
  u8g2.setFontPosTop();
  u8g2.setFont(NatRailTall12);

  if (carouselActive) centreText("Moving to next carousel slot",80);
  else if (schedulerActive) centreText("Moving to next scheduler slot",80);
  else centreText("Switching modes",80);
  u8g2.setFont(bodyFont());
  centreText("Waiting for background process to complete...",115);
#else
  u8g2.setFont(NatRailTall12);

  if (carouselActive) centreText("Moving to next carousel slot",20);
  else if (schedulerActive) centreText("Moving to next scheduler slot",20);
  else centreText("Switching modes",20);
  u8g2.setFont(bodyFont());
  centreText("Waiting for background process to complete...",42);
#endif
  u8g2.sendBuffer();
}

/*
 * Utility functions
*/

// Saves a file (string) to the FFS
bool saveFile(String fName, String fData) {
  File f = LittleFS.open(fName,"w");
  if (f) {
    f.println(fData);
    f.close();
    return true;
  } else return false;
}

// Loads a file (string) from the FFS
String loadFile(String fName) {
  File f = LittleFS.open(fName,"r");
  if (f) {
    String result = f.readString();
    f.close();
    return result;
  } else return "";
}

// Get the Build Timestamp of the running firmware
String getBuildTime() {
  char timestamp[22];
  char buildtime[11];
  struct tm tm = {};

  sprintf(timestamp,"%s %s",__DATE__,__TIME__);
  strptime(timestamp,"%b %d %Y %H:%M:%S",&tm);
  sprintf(buildtime,"%02d%02d%02d%02d%02d",tm.tm_year-100,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min);
  return String(buildtime);
}

void checkPostWebUpgrade() {
  JsonDocument doc;
  char prevFirmware[15] = "B0.0-W0.0";
  char prevGUI[8];
  char currentGUI[8];

  if (LittleFS.exists("/fw.json")) {
    File file = LittleFS.open("/fw.json", "r");
    if (file) {
      DeserializationError error = deserializeJson(doc, file);
      if (!error) {
        JsonObject settings = doc.as<JsonObject>();

        if (settings["fw"].is<const char*>()) {
          strlcpy(prevFirmware,settings["fw"],sizeof(prevFirmware));
        }
      }
      file.close();
    }
  }

  if (prevFirmware[0]) {
    sscanf(prevFirmware,"%*[^ -]-%s",prevGUI);
    sprintf(currentGUI,"W%d.%d",WEBAPPVER_MAJOR,WEBAPPVER_MINOR);
    if (strcmp(prevGUI,currentGUI)) {
      // clean up old/dev files
      progressBar("Cleaning up following upgrade",45);
      LittleFS.remove("/index_d.htm");
      LittleFS.remove("/index.htm");
      LittleFS.remove("/keys.htm");
      LittleFS.remove("/nrelogo.webp");
      LittleFS.remove("/rdglogo.webp");
      LittleFS.remove("/tfllogo.webp");
      LittleFS.remove("/btlogo.webp");
      LittleFS.remove("/tube.webp");
      LittleFS.remove("/nr.webp");
      LittleFS.remove("/favicon.svg");
      LittleFS.remove("/favicon.png");
      LittleFS.remove("/webver");
    }
  }
}

// Returns true if sleep mode is enabled and we're within the sleep period
bool isSnoozing() {
  if (forcedSleep || NSEclockIsActive) return true;
  if (forcedAwake) {
    if (button.secsSinceLastTap() >= stayAwakeSeconds) forcedAwake = false;
    else return false;
  }
  if (!sleepEnabled) return false;
  byte myHour = timeinfo.tm_hour;
  if (sleepStarts > sleepEnds) {
    if ((myHour >= sleepStarts) || (myHour < sleepEnds)) return true; else return false;
  } else {
    if ((myHour >= sleepStarts) && (myHour < sleepEnds)) return true; else return false;
  }
}

// Stores/updates the url of our Web GUI
void updateMyUrl() {
  IPAddress ip = WiFi.localIP();
  snprintf(myUrl,sizeof(myUrl),"http://%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]);
}

/*
 * Start-up configuration functions
 */

// Load the API keys from the file system (if they exist)
void loadApiKeys() {
  JsonDocument doc;

  if (LittleFS.exists("/apikeys.json")) {
    File file = LittleFS.open("/apikeys.json", "r");
    if (file) {
      DeserializationError error = deserializeJson(doc, file);
      if (!error) {
        JsonObject settings = doc.as<JsonObject>();

        if (settings["rdmDepKey"].is<const char*>()) {
          rdmDeparturesApiKey = settings["rdmDepKey"].as<String>();
        }

        if (settings["rdmSvcKey"].is<const char*>()) {
          rdmServiceApiKey = settings["rdmSvcKey"].as<String>();
        }

        if (settings["nrToken"].is<const char*>()) {
          strlcpy(nrToken, settings["nrToken"], sizeof(nrToken));
        }

        if (settings["owmToken"].is<const char*>()) {
          strlcpy(openWeatherMapApiKey, settings["owmToken"], sizeof(openWeatherMapApiKey));
        }

        if (settings["appKey"].is<const char*>()) {
          strlcpy(tflAppKey,settings["appKey"],sizeof(tflAppKey));
        }
        apiKeys = true;

      } else {
        // JSON deserialization failed - TODO
      }
      file.close();
    }
  }
}

void resetLocationIds() {
  strcpy(locationCode,"");
  railIsSet = false;
  tubeIsSet = false;
  busIsSet = false;
}

void saveFirmwareInfo() {
  String fw = "{\"fw\":\"B" + String(VERSION_MAJOR) + "." + String(VERSION_MINOR) + "-W" + String(WEBAPPVER_MAJOR) + "." + String(WEBAPPVER_MINOR) + "\"}";
  saveFile("/fw.json",fw);
}

// Write a default config file so that the Web GUI works initially (force Tube mode if no NR token)
void writeDefaultConfig() {
  #if defined(DISPLAY_CYD)
  int defaultBrightness = 200;
  bool defaultTouch = true;
  #else
  int defaultBrightness = 20;
  bool defaultTouch = false;
  #endif

  String defaultConfig = "{\"crs\":\"\",\"station\":\"\",\"lat\":0,\"lon\":0,\"weather\":true,\"sleep\":false,\"showDate\":false,\"showBus\":false,\"update\":true,\"sleepStarts\":23,\"sleepEnds\":8,\"brightness\":" + String(defaultBrightness) + ",\"touch\":" + String(defaultTouch ? "true" : "false") + ",\"webAutoScroll\":true,\"webAutoScrollSpeed\":60,\"displayColor\":0,\"displayScale\":0,\"tubeId\":\"\",\"tubeName\":\"\",\"mode\":" + String((!nrToken[0] && rdmDeparturesApiKey=="")?"1":"0") + "}";
  saveFile("/config.json",defaultConfig);
  strcpy(webPassword, "");
  strcpy(webUsername, "admin");
  resetLocationIds();
  saveFirmwareInfo();
}

bool pruneFromPhrase(char* input, const char* target) {
  // Find the first occurance of the target word or phrase
  char* pos = strstr(input,target);
  // If found, prune from here
  if (pos) {
      input[pos - input] = '\0';
      return true;
  }
  return false;
}

int getTimeInMinutes() {
  return (timeinfo.tm_hour * 60 + timeinfo.tm_min);
}

void loadSlot(JsonObjectConst slot, bool isDefault, boardModes requestedMode) {
  if (requestedMode == MODE_NEXTMODE) {
    switch (boardMode) {
      case MODE_RAIL:
        if (tubeIsSet) boardMode = MODE_TUBE;
        else if (busIsSet) boardMode = MODE_BUS;
        break;
      case MODE_TUBE:
        if (busIsSet) boardMode = MODE_BUS;
        else if (railIsSet) boardMode = MODE_RAIL;
        break;
      case MODE_BUS:
        if (railIsSet) boardMode = MODE_RAIL;
        else if (tubeIsSet) boardMode = MODE_TUBE;
        break;
    }
  } else {
    if (slot["mode"].is<int>()) boardMode = slot["mode"];
    else if (slot["tube"].is<bool>()) boardMode = slot["tube"] ? MODE_TUBE : MODE_RAIL; // handle legacy v1.x config
  }

  switch (boardMode) {
    case MODE_RAIL:
      if (slot["crs"].is<const char*>())              strlcpy(locationCode, slot["crs"], sizeof(locationCode));
      if (slot["platformFilter"].is<const char*>())   strlcpy(locationFilter, slot["platformFilter"], sizeof(locationFilter));
      if (slot["callingCrs"].is<const char*>())       strlcpy(callingCrsCode, slot["callingCrs"], sizeof(callingCrsCode));
      if (slot["callingStation"].is<const char*>())   strlcpy(callingStation, slot["callingStation"], sizeof(callingStation));
      if (slot["lat"].is<float>())                    locationLat = slot["lat"];
      if (slot["lon"].is<float>())                    locationLon = slot["lon"];
      break;

    case MODE_TUBE:
      if (slot["tubeId"].is<const char*>())     strlcpy(locationCode, slot["tubeId"], sizeof(locationCode));
      if (slot["lineid"].is<const char*>())     strlcpy(lineId, slot["lineid"], sizeof(lineId));
      if (slot["direction"].is<const char*>())  strlcpy(lineDirection, slot["direction"], sizeof(lineDirection));
      if (isDefault) {
        if (slot["tubeName"].is<const char*>()) strlcpy(locationName, slot["tubeName"], sizeof(locationName));
        if (slot["tubeLat"].is<float>())      locationLat = slot["tubeLat"];
        if (slot["tubeLon"].is<float>())      locationLon = slot["tubeLon"];
        pruneFromPhrase(locationName," Underground Station");
        pruneFromPhrase(locationName," DLR Station");
        pruneFromPhrase(locationName," (H&C Line)");
      } else {
        if (slot["name"].is<const char*>()) strlcpy(locationName, slot["name"], sizeof(locationName));
        if (slot["lat"].is<float>())          locationLat = slot["lat"];
        if (slot["lon"].is<float>())          locationLon = slot["lon"];
      }
      break;

    case MODE_BUS:
      if (slot["busId"].is<const char*>())      strlcpy(locationCode, slot["busId"], sizeof(locationCode));
      if (slot["busFilter"].is<const char*>())  strlcpy(locationFilter, slot["busFilter"], sizeof(locationFilter));
      if (isDefault) {
        if (slot["busName"].is<const char*>())    strlcpy(locationName, slot["busName"], sizeof(locationName));
        if (slot["busLat"].is<float>())           locationLat = slot["busLat"];
        if (slot["busLon"].is<float>())           locationLon = slot["busLon"];
      } else {
        if (slot["name"].is<const char*>())   strlcpy(locationName, slot["name"], sizeof(locationName));
        if (slot["lat"].is<float>())          locationLat = slot["lat"];
        if (slot["lon"].is<float>())          locationLon = slot["lon"];
      }
      break;

  }
}

// Load the configuration settings (if they exist, if not create a default set for the Web GUI page to read)
void loadConfig(bool coldBoot = false, boardModes requestedMode = MODE_LOADCONFIG) {
  JsonDocument doc;

  // Set defaults
  strcpy(hostname,defaultHostname);
  strcpy(lineId,"all");
  strcpy(lineDirection,"");

  timezone = String(ukTimezone);
  resetLocationIds();

  schedulerActive = false;
  carouselActive = false;

  if (LittleFS.exists("/config.json")) {
    File file = LittleFS.open("/config.json", "r");
    if (file) {
      DeserializationError error = deserializeJson(doc, file);
      if (!error) {
        JsonObject settings = doc.as<JsonObject>();

        // Load common settings
        if (settings["crs"].is<const char*>() && strlen(settings["crs"])) railIsSet = true; else railIsSet = false;
        if (settings["tubeId"].is<const char*>() && strlen(settings["tubeId"])) tubeIsSet = true; else tubeIsSet = false;
        if (settings["busId"].is<const char*>() && strlen(settings["busId"])) busIsSet = true; else busIsSet = false;

        if (settings["hostname"].is<const char*>())   strlcpy(hostname, settings["hostname"], sizeof(hostname));
        if (settings["wsdlHost"].is<const char*>())   strlcpy(wsdlHost, settings["wsdlHost"], sizeof(wsdlHost));
        if (settings["wsdlAPI"].is<const char*>())    strlcpy(wsdlAPI, settings["wsdlAPI"], sizeof(wsdlAPI));
        if (settings["showDate"].is<bool>())          dateEnabled = settings["showDate"];
        if (settings["showBus"].is<bool>())           enableBus = settings["showBus"];
        if (settings["showFullCalling"].is<bool>())   showFullCalling = settings["showFullCalling"];
        if (settings["showFullMsgs"].is<bool>())      showFullMsgs = settings["showFullMsgs"];
        if (settings["showClockNoServices"].is<bool>()) showClockNoServices = settings["showClockNoServices"];
        if (settings["sleep"].is<bool>())             sleepEnabled = settings["sleep"];
        if (settings["darkSleep"].is<bool>())         sleepClock = !settings["darkSleep"];
        if (settings["fastRefresh"].is<bool>())       apiRefreshRate = settings["fastRefresh"] ? FASTDATAUPDATEINTERVAL : DATAUPDATEINTERVAL;
        if (settings["weather"].is<bool>())           weatherEnabled = settings["weather"];
        if (settings["update"].is<bool>())            firmwareUpdates = settings["update"];
        if (settings["updateDaily"].is<bool>())       dailyUpdateCheck = settings["updateDaily"];
        if (settings["sleepStarts"].is<int>())        sleepStarts = settings["sleepStarts"];
        if (settings["sleepEnds"].is<int>())          sleepEnds = settings["sleepEnds"];
        if (settings["brightness"].is<int>())         brightness = settings["brightness"];
        if (settings["longPressClock"].is<bool>())    longPressClock = settings["longPressClock"];
        if (settings["sleepUseBigClock"].is<bool>())  useNSEclockForSleep = (settings["sleepUseBigClock"] && sleepClock);

        if (settings["noScroll"].is<bool>())          noScrolling = settings["noScroll"];
        if (settings["flip"].is<bool>())              flipScreen = settings["flip"];
        if (settings["touch"].is<bool>())             touchEnabled = settings["touch"];
#if defined(DISPLAY_CYD)
        if (settings["displayColor"].is<int>())        u8g2.setColorScheme((CydColorScheme)settings["displayColor"].as<int>());
        if (settings["displayScale"].is<int>())        u8g2.setScaleMode((CydScaleMode)settings["displayScale"].as<int>());
#endif
        if (settings["dataIcon"].is<bool>())          showDataIcon = settings["dataIcon"];
        if (settings["forceWakeTime"].is<int>())      stayAwakeSeconds = settings["forceWakeTime"];
        if (settings["TZ"].is<const char*>())         timezone = settings["TZ"].as<String>();
        if (settings["nrTimeOffset"].is<int>())       nrTimeOffset = settings["nrTimeOffset"];
        if (settings["hidePlatform"].is<bool>())      hidePlatform = settings["hidePlatform"];
        if (settings["hideOrdinals"].is<bool>())      hideOrdinals = settings["hideOrdinals"];
        if (settings["showLastSeen"].is<bool>())      showLastSeen = settings["showLastSeen"];
        if (settings["showTubeLocation"].is<bool>())  showTubeCurrentLocation = settings["showTubeLocation"];
        if (settings["showServiceMsgs"].is<bool>())   showServiceMsgs = settings["showServiceMsgs"];

        if (settings["enableScheduler"].is<bool>())   enableScheduler = settings["enableScheduler"];
        if (settings["enableCarousel"].is<bool>())    enableCarousel = settings["enableCarousel"];

        if (settings["rssUrl"].is<const char*>())     rssURL = settings["rssUrl"].as<String>();
        if (settings["rssName"].is<const char*>())    rssName = settings["rssName"].as<String>();
        if (rssURL != "") rssEnabled = true; else rssEnabled = false;
        if (settings["rssPriority"].is<bool>())       rssPriority = settings["rssPriority"];

        if (settings["webPassword"].is<const char*>()) strlcpy(webPassword, settings["webPassword"], sizeof(webPassword));
        else strcpy(webPassword, "");
        if (settings["webUsername"].is<const char*>() && strlen(settings["webUsername"]) > 0) strlcpy(webUsername, settings["webUsername"], sizeof(webUsername));
        else strcpy(webUsername, "admin");

        if (requestedMode != MODE_NEXTMODE) {
          if (settings["mode"].is<int>())             boardMode = settings["mode"];
          else if (settings["tube"].is<bool>())       boardMode = settings["tube"] ? MODE_TUBE : MODE_RAIL; // handle legacy v1.x config
        }

        if (settings["dataSource"].is<int>())         useRDMclient = (settings["dataSource"]?1:0);
        // validate the data source against which api keys are available
        if (nrToken[0] && rdmDeparturesApiKey=="") useRDMclient = false;
        else if (!nrToken[0] && rdmDeparturesApiKey!="") useRDMclient = true;

        if (coldBoot) {
          // Just load base parameters at boot, clock not set yet so exit
          file.close();
          return;
        }

        // Work out what board mode we're in
        JsonArray scheduler = settings["scheduler"].as<JsonArray>();
        JsonArray carousel = settings["carousel"].as<JsonArray>();

        if (enableScheduler && !scheduler.isNull() && scheduler.size() > 0 && (requestedMode==MODE_LOADCONFIG || requestedMode==MODE_NEXTSCHEDULE)) {
          if (requestedMode == MODE_LOADCONFIG) {
            int currentMins = getTimeInMinutes();
            int activeIndex = -1;

            // Iterate through the sorted schedule to find the last entry that has already started
            for (int i = 0; i < scheduler.size(); i++) {
              const char* timeStr = scheduler[i]["time"];
              if (timeStr) {
                int h, m;
                if (sscanf(timeStr, "%d:%d", &h, &m) == 2) {
                  int entryMins = h * 60 + m;
                  if (entryMins <= currentMins) {
                    activeIndex = i;
                  } else {
                    // Since the array is chronologically sorted, the first entry strictly
                    // greater than the current time implies we've passed the active one.
                    break;
                  }
                }
              }
            }

            // If activeIndex is still -1, it means the current time is before the first entry
            // of the day. Because of midnight wrap-around, the active entry is the LAST entry
            // from the previous day.
            if (activeIndex == -1) {
              activeIndex = scheduler.size() - 1;
            }

            // Save the active entry index (for touch)
            currentScheduleSlot = activeIndex;
            numScheduleSlots = scheduler.size();

            // The next entry simply follows the active one, wrapping back to 0 at the end of the array
            int nextIndex = (activeIndex + 1) % scheduler.size();

            JsonObject currentSchedule = scheduler[activeIndex];
            loadSlot(currentSchedule,false,requestedMode);
            schedulerActive = true;

            // Save the time of the active entry
            const char* timeStrActive = scheduler[activeIndex]["time"];
            if (timeStrActive) {
              int h, m;
              if (sscanf(timeStrActive, "%d:%d", &h, &m) == 2) {
                activeSlotEventTime = h * 60 + m; // The time (in minutes) of the active event
              }
            }

            // Save the time of the next entry (for the loop scheduler)
            const char* timeStr = scheduler[nextIndex]["time"];
            if (timeStr) {
              int h, m;
              if (sscanf(timeStr, "%d:%d", &h, &m) == 2) {
                nextSlotEventTime = h * 60 + m; // The time (in minutes) of the next change
              }
            }
          } else {
            // Push on to the next schedule slot via touch
            currentScheduleSlot = (currentScheduleSlot + 1) % numScheduleSlots;
            JsonObject currentSchedule = scheduler[currentScheduleSlot];
            loadSlot(currentSchedule,false,requestedMode);
            schedulerActive = true;
          }
        } else if (enableCarousel && !carousel.isNull() && carousel.size() > 0 && requestedMode == MODE_LOADCONFIG) {
          numCarouselSlots = carousel.size();
          if (currentCarouselSlot >= numCarouselSlots) currentCarouselSlot = 0;
          JsonObject currentCarousel = carousel[currentCarouselSlot];
          loadSlot(currentCarousel,false,requestedMode);
          carouselActive = true;

          // Work out when the next change occurs
          activeSlotEventTime = getTimeInMinutes();
          if (currentCarousel["duration"].is<int>()) nextSlotEventTime = currentCarousel["duration"];
          nextSlotEventTime = (activeSlotEventTime + nextSlotEventTime) % 1440;

        } else {
          // Plain board mode as defined by the user
          loadSlot(settings,true,requestedMode);
        }
      } else {
        // JSON deserialization failed - TODO
      }
      file.close();
    }
  } else if (apiKeys) writeDefaultConfig();
}

void buildRssMessage() {
  if (rss.numRssTitles>0) {
    sprintf(rssMessage,"%s: %s",rssName.c_str(),rss.rssTitle[0]);
    for (int i=1;i<rss.numRssTitles;i++) {
      if (strlen(rssMessage) + strlen(rss.rssTitle[i]) + 1 < MAXMESSAGESIZE) {
        strcat(rssMessage,"\x90");
        strcat(rssMessage,rss.rssTitle[i]);
      } else {
        break;
      }
    }
  } else {
    rssMessage[0] = '\0';
  }
}

void updateRssFeed() {
  if (lastRssUpdateResult=rss.loadFeed(rssURL); lastRssUpdateResult == UPD_SUCCESS) {
    nextRssUpdate = millis() + RSSUPDATEINTERVAL; // update every ten minutes
    buildRssMessage();
  }
  else nextRssUpdate = millis() + (RSSUPDATEINTERVAL/2); // Failed so try again in 5 minutes
}

// Update the current weather message if weather updates are enabled and we have a lat/lon for the selected location
void updateCurrentWeather(float latitude, float longitude) {
  nextWeatherUpdate = millis() + WEATHERUPDATEINTERVAL;
  if (!latitude || !longitude) return; // No location co-ordinates
  weatherMsg[0]='\0';
  lastWeatherUpdateResult = currentWeather.updateWeather(openWeatherMapApiKey, latitude, longitude);
  if (lastWeatherUpdateResult == UPD_SUCCESS) strlcpy(weatherMsg,currentWeather.currentWeatherMessage,MAXWEATHERSIZE);
}

void checkWeatherUpdate(float prevLat, float prevLon) {
  if (weatherEnabled && (prevLat!=locationLat || prevLon!=locationLon)) {
    prevProgressBarPosition = 114;
    progressBar("Getting weather conditions",60);
    updateCurrentWeather(locationLat,locationLon);
  }
}

// Soft reset/reload the board.
void softResetBoard(boardModes requestedMode) {
  boardModes previousMode = boardMode;
  String prevRssUrl = rssURL;
  float prevLat = locationLat;
  float prevLon = locationLon;
  bool prevWeatherEnabled = weatherEnabled;

  // Reload the settings
  loadConfig(false,requestedMode);
  if (flipScreen) u8g2.setFlipMode(1); else u8g2.setFlipMode(0);
  if (timezone!="") {
    setenv("TZ",timezone.c_str(),1);
  } else {
    setenv("TZ",ukTimezone,1);
  }
  tzset();
  u8g2.clearBuffer();
#if defined(DISPLAY_CYD)
  u8g2.setTextScale(1);
  u8g2.setFontPosTop();
#endif
  drawStartupHeading();
#if defined(DISPLAY_CYD)
  if (requestedMode==MODE_NEXTMODE) centreText("Switching modes...",110);
#else
  if (requestedMode==MODE_NEXTMODE) centreText("Switching modes...",53);
#endif
  u8g2.updateDisplay();

  // Force an update asap
  nextDataUpdate = 0;
  nextWeatherUpdate = millis()+60000; // Ensure the weather is updated after the data feed
  nextRssUpdate += 30000;
  isScrollingService = false;
  isScrollingStops = false;
  isScrollingPrimary = false;
  noServiceClockIsActive = false;
  isSleeping = false;
  forcedSleep = false;
  firstLoad = true;
  noDataLoaded = true;
  viaTimer = 0;
  timer = 0;
  serviceTimer = 0;
  prevProgressBarPosition = 133;
  startupProgressPercent = 70;
  currentMessage = 0;
  prevMessage = 0;
  prevScrollStopsLength = 0;
  isShowingVia = false;
  line3Service = 0;
  prevService = 0;
  fetchComplete = false;
  nextSchedulerCheck = millis()+10000;
  if (!weatherEnabled) weatherMsg[0] = '\0';
  else if (!prevWeatherEnabled) {
    // force a weather update, even if the location hasn't changed
    prevLat = 0;
    prevLon = 0;
  }

  if (rssEnabled && prevRssUrl != rssURL) {
    rssMessage[0] = '\0';
    if (boardMode == MODE_RAIL || boardMode == MODE_TUBE) {
      prevProgressBarPosition = 95;
      progressBar("Updating RSS headlines feed",50);
      updateRssFeed();
    }
  } else if (rssEnabled && previousMode != boardMode && boardMode != MODE_BUS) {
    buildRssMessage();
  }

  switch (boardMode) {
    case MODE_RAIL:
      checkWeatherUpdate(prevLat,prevLon);
      // Create a cleaned platform filter (if any)
      rdmRailData.cleanFilter(locationFilter,locationCleanFilter,sizeof(locationFilter));
      progressBar("Initialising National Rail interface",70);
      if (!useRDMclient) {
        // Using legacy XML client
        int res = darwinRailData.init(wsdlHost, wsdlAPI);
        if (res != UPD_SUCCESS) {
          showWsdlFailureScreen();
          while (true) { delay(1);}
        }
      }
      break;

    case MODE_TUBE:
      checkWeatherUpdate(prevLat,prevLon);
      progressBar("Initialising TfL interface",70);
      break;

    case MODE_BUS:
      checkWeatherUpdate(prevLat,prevLon);
      progressBar("Initialising BusTimes interface",70);
      // Create a cleaned filter
      busdata.cleanFilter(locationFilter,locationCleanFilter,sizeof(locationFilter));
      break;
  }
  station.numServices=0;
  messages.numMessages=0;
}

// Handle switching to next board mode or carousel/scheduler slot (touch sensor)
void switchToNextMode() {
  if ((carouselActive && numCarouselSlots<2) || (schedulerActive && numScheduleSlots<2)) return;  // Nothing to switch to

  if (fetchInProgress) {
    // Wait for the background fetch to finish before we soft reset
    showSwitchScreen();
    while (fetchInProgress) delay(50);
  }

  if (carouselActive) {
    currentCarouselSlot = (currentCarouselSlot + 1) % numCarouselSlots;
    softResetBoard(MODE_LOADCONFIG);
  }
  else if (schedulerActive) softResetBoard(MODE_NEXTSCHEDULE);
  else if (railIsSet+tubeIsSet+busIsSet > 1) softResetBoard(MODE_NEXTMODE); // Check there's at least two configured modes
}

// WiFiManager callback, entered config mode
void wmConfigModeCallback (WiFiManager *myWiFiManager) {
  showSetupScreen();
  wifiConfigured = true;
}

/*
 * Firmware / Web GUI Update functions
*/
bool isFirmwareUpdateAvailable() {
  int releaseMajor = ghUpdate.releaseId.substring(1,ghUpdate.releaseId.indexOf(".")).toInt();
  int releaseMinor = ghUpdate.releaseId.substring(ghUpdate.releaseId.indexOf(".")+1,ghUpdate.releaseId.indexOf("-")).toInt();
  if (VERSION_MAJOR > releaseMajor) return false;
  if ((VERSION_MAJOR == releaseMajor) && (VERSION_MINOR >= releaseMinor)) return false;
  return true;
}

// Callback function for displaying firmware update progress
void update_progress(int cur, int total) {
  int percent = ((cur * 100)/total);
  showFirmwareUpdateProgress(percent);
}

// Attempts to install newer firmware if available
bool checkForFirmwareUpdate() {
  bool result = true;

  if (!isFirmwareUpdateAvailable()) return result;

  // Check that we found the firmware.bin file in the release assets
  if (ghUpdate.firmwareURL.length()==0) return result;

  showFirmwareUpdateWarningScreen(ghUpdate.releaseDescription.c_str());
  u8g2.clearDisplay();
  prevProgressBarPosition=0;
  showFirmwareUpdateProgress(0);  // So we don't have a blank screen
  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.onProgress(update_progress);
  httpUpdate.rebootOnUpdate(false); // Don't auto reboot, we'll handle it

  HTTPUpdateResult ret = httpUpdate.handleUpdate(client, ghUpdate.firmwareURL);
  const char* msgTitle = "Firmware Update";
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      char msg[60];
      sprintf(msg,"The update failed with error %d.",httpUpdate.getLastError());
      result=false;
      for (int i=20;i>=0;i--) {
        showUpdateCompleteScreen(msgTitle,msg,httpUpdate.getLastErrorString().c_str(),"",i,false);
        delay(1000);
      }
      break;

    case HTTP_UPDATE_NO_UPDATES:
      for (int i=10;i>=0;i--) {
        showUpdateCompleteScreen(msgTitle,"","No firmware updates were available.","",i,false);
        delay(1000);
      }
      break;

    case HTTP_UPDATE_OK:
      for (int i=20;i>=0;i--) {
        showUpdateCompleteScreen(msgTitle,"The firmware update has completed successfully.","For more information visit the URL below:","github.com/gadec-uk/departures-board/releases",i,true);
        delay(1000);
      }
      ESP.restart();
      break;
  }
  u8g2.clearDisplay();
  drawStartupHeading();
  u8g2.sendBuffer();
  return result;
}

/*
 * Station Board functions - pulling updates and animating the Departures Board main display
 */

// Draw the primary service line
#if defined(DISPLAY_CYD)
void drawPrimaryServiceAt(const rdService &service, bool platformAvailable, bool showVia, int y);
#endif

void drawPrimaryService(bool showVia) {
#if defined(DISPLAY_CYD)
  blankArea(0,LINE1,256,LINE2-LINE1);
  drawPrimaryServiceAt(station.service[0],station.platformAvailable,showVia,LINE1);
  return;
#else
  int destPos;
  char clipDestination[MAXLOCATIONSIZE+5];
  char etd[16];
  char plat[9];

  u8g2.setFont(NatRailTall12);
  const int primaryBaseline = railDetailBaseline(LINE1);
  blankArea(0,LINE1,256,LINE2-LINE1);
  destPos = u8g2.drawStr(0,primaryBaseline,station.service[0].sTime) + 6;
  if (isDigit(station.service[0].etd[0])) sprintf(etd,"Exp %s",station.service[0].etd);
  else strcpy(etd,station.service[0].etd);
  int etdWidth = getStringWidth(etd) + (etd[strlen(etd)-1]=='1'?1:0);
  u8g2.drawStr(SCREEN_WIDTH - etdWidth,primaryBaseline,etd);
  int spaceAvailable = SCREEN_WIDTH - destPos - etdWidth - 6;

  if (station.platformAvailable && station.service[0].platform[0] && station.service[0].serviceType == TRAIN && !hidePlatform) {
    sprintf(plat,"Plat %.3s",station.service[0].platform);
    int platWidth = getStringWidth(plat) + (plat[strlen(plat)-1]=='1'?1:0);;
    u8g2.drawStr(SCREEN_WIDTH - etdWidth - platWidth - 7,primaryBaseline,plat);
    spaceAvailable-=(platWidth+7);
  }

  if (showVia) strcpy(clipDestination,station.service[0].via);
  else {
    strcpy(clipDestination,station.service[0].destination);
    if (station.service[0].serviceType == BUS) strcat(clipDestination," ~");  // Add bus icon to destination
  }
  if (getStringWidth(clipDestination) > spaceAvailable) {
    while (getStringWidth(clipDestination) > (spaceAvailable - 8)) {
      clipDestination[strlen(clipDestination)-1] = '\0';
    }
    // check if there's a trailing space left
    if (clipDestination[strlen(clipDestination)-1] == ' ') clipDestination[strlen(clipDestination)-1] = '\0';
    strcat(clipDestination,"...");
  }
  u8g2.drawStr(destPos,primaryBaseline,clipDestination);
  setRailDetailFont();
#endif
}

#if defined(DISPLAY_CYD)
void drawPrimaryServiceAt(const rdService &service, bool platformAvailable, bool showVia, int y) {
  uint8_t previousTextScale = u8g2.getTextScale();
  u8g2.setTextScale(1);
  setRailDetailFont();
  const int primaryBaseline = railDetailBaseline(y);
  const int primaryInfoBaseline = railDetailBaseline(y + CYD_SECOND_SERVICE_INFO_OFFSET);
  char clipDestination[MAXLOCATIONSIZE+5];
  char etd[16];
  char plat[9];

  int destPos = u8g2.drawStr(0,primaryBaseline,service.sTime) + 6;
  if (showVia) strcpy(clipDestination,service.via);
  else {
    strcpy(clipDestination,service.destination);
    if (service.serviceType == BUS) strcat(clipDestination," ~");
  }
  int spaceAvailable = SCREEN_WIDTH - destPos - 6;
  if (getStringWidth(clipDestination) > spaceAvailable) {
    while (getStringWidth(clipDestination) > (spaceAvailable - 8)) {
      clipDestination[strlen(clipDestination)-1] = '\0';
    }
    if (clipDestination[strlen(clipDestination)-1] == ' ') clipDestination[strlen(clipDestination)-1] = '\0';
    strcat(clipDestination,"...");
  }
  u8g2.drawStr(destPos,primaryBaseline,clipDestination);

  if (isDigit(service.etd[0])) sprintf(etd,"Exp %s",service.etd);
  else strcpy(etd,service.etd);
  int etdWidth = getStringWidth(etd) + (etd[strlen(etd)-1]=='1'?1:0);
  u8g2.drawStr(CYD_DETAIL_STATUS_RIGHT - etdWidth,primaryInfoBaseline,etd);

  if (platformAvailable && service.platform[0] && service.serviceType == TRAIN && !hidePlatform) {
    sprintf(plat,"Plat %.3s",service.platform);
    u8g2.drawStr(0,primaryInfoBaseline,plat);
  }
  u8g2.setTextScale(previousTextScale);
}

void beginCydPrimaryServiceAnimation(const rdService *previousService, bool previousPlatformAvailable, bool previousShowVia) {
  cydPrimaryServiceHasPrevious = previousService != nullptr;
  if (previousService) {
    cydPreviousPrimaryService = *previousService;
    cydPreviousPrimaryPlatformAvailable = previousPlatformAvailable;
    cydPreviousPrimaryShowVia = previousShowVia;
  }
  cydPrimaryServiceScrollY = CYD_PRIMARY_SERVICE_SCROLL_DISTANCE;
  isScrollingCydPrimaryService = true;
}

bool cydPrimaryServiceChanged(const rdService &previousService, bool previousPlatformAvailable) {
  const rdService &service = station.service[0];
  return strcmp(previousService.sTime,service.sTime) != 0 ||
         strcmp(previousService.destination,service.destination) != 0 ||
         strcmp(previousService.via,service.via) != 0 ||
         strcmp(previousService.etd,service.etd) != 0 ||
         strcmp(previousService.platform,service.platform) != 0 ||
         previousService.serviceType != service.serviceType ||
         previousPlatformAvailable != station.platformAvailable;
}

void drawCydPrimaryServiceTransition(bool showVia) {
  blankArea(0,LINE1,SCREEN_WIDTH,CYD_PRIMARY_SERVICE_SCROLL_DISTANCE);
  u8g2.setClipWindow(0,LINE1,SCREEN_WIDTH,PRIMARY_MESSAGE_LINE);
  if (cydPrimaryServiceHasPrevious) {
    drawPrimaryServiceAt(cydPreviousPrimaryService,cydPreviousPrimaryPlatformAvailable,cydPreviousPrimaryShowVia,
                         LINE1 - (CYD_PRIMARY_SERVICE_SCROLL_DISTANCE - cydPrimaryServiceScrollY));
  }
  drawPrimaryServiceAt(station.service[0],station.platformAvailable,showVia,LINE1 + cydPrimaryServiceScrollY);
  u8g2.setMaxClipWindow();
}

void addCydPrimaryMessage(const char *message) {
  if (!message[0] || cydPrimaryMessageCount >= (int)(sizeof(cydPrimaryMessages) / sizeof(cydPrimaryMessages[0]))) return;
  strlcpy(cydPrimaryMessages[cydPrimaryMessageCount++],message,sizeof(cydPrimaryMessages[0]));
}

void drawPrimaryServiceMessage() {
  u8g2.setTextScale(1);
  setRailDetailFont();
  blankArea(0,PRIMARY_MESSAGE_LINE,SCREEN_WIDTH,20);
  if (!cydPrimaryMessageCount) {
    displayedPrimaryServiceMessage[0] = '\0';
    return;
  }

  const char *message = cydPrimaryMessages[cydPrimaryMessageIndex];
  if (strcmp(displayedPrimaryServiceMessage,message)) {
    strlcpy(displayedPrimaryServiceMessage,message,sizeof(displayedPrimaryServiceMessage));
    primaryServiceMessageScrollX = 0;
    primaryServiceMessageWidth = getStringWidth(displayedPrimaryServiceMessage);
    primaryServiceMessageTimer = millis() + 6000;
  }

  u8g2.setClipWindow(0,PRIMARY_MESSAGE_LINE,SCREEN_WIDTH,PRIMARY_MESSAGE_LINE+20);
  if (primaryServiceMessageWidth <= SCREEN_WIDTH) {
    centreText(displayedPrimaryServiceMessage,railDetailBaseline(PRIMARY_MESSAGE_LINE));
    if (millis() >= primaryServiceMessageTimer) {
      cydPrimaryMessageIndex = (cydPrimaryMessageIndex + 1) % cydPrimaryMessageCount;
      displayedPrimaryServiceMessage[0] = '\0';
    }
  } else {
    u8g2.drawStr(primaryServiceMessageScrollX,railDetailBaseline(PRIMARY_MESSAGE_LINE),displayedPrimaryServiceMessage);
    primaryServiceMessageScrollX--;
    if (primaryServiceMessageScrollX < -primaryServiceMessageWidth) {
      cydPrimaryMessageIndex = (cydPrimaryMessageIndex + 1) % cydPrimaryMessageCount;
      displayedPrimaryServiceMessage[0] = '\0';
    }
  }
  u8g2.setMaxClipWindow();
}

void drawSecondaryServiceLine(int serviceIndex, int y = LINE2, bool blank = true) {
  const int serviceBaseline = railDetailBaseline(y);
  const int infoBaseline = railDetailBaseline(y + CYD_SECOND_SERVICE_INFO_OFFSET);
  char clipDestination[MAXLOCATIONSIZE+5];
  char etd[16];
  char ordinal[8];
  char plat[9];

  u8g2.setTextScale(1);
  setRailDetailFont();
  if (blank) blankArea(0,y,SCREEN_WIDTH,LINE3-LINE2);

  if (station.numServices <= 1 || serviceIndex <= 0 || serviceIndex >= station.numServices) return;

  switch (serviceIndex) {
    case 1:
      strcpy(ordinal,"2nd ");
      break;
    case 2:
      strcpy(ordinal,"3rd ");
      break;
    default:
      sprintf(ordinal,"%dth ",serviceIndex+1);
      break;
  }

  int destPos;
  if (hideOrdinals) {
    destPos = u8g2.drawStr(0,serviceBaseline,station.service[serviceIndex].sTime) + 6;
  } else {
    int timeX = u8g2.drawStr(0,serviceBaseline,ordinal) + 6;
    destPos = timeX + u8g2.drawStr(timeX,serviceBaseline,station.service[serviceIndex].sTime) + 6;
  }
  strcpy(clipDestination,station.service[serviceIndex].destination);
  if (station.service[serviceIndex].serviceType == BUS) strcat(clipDestination," ~");
  int spaceAvailable = SCREEN_WIDTH - destPos - 6;
  if (getStringWidth(clipDestination) > spaceAvailable) {
    while (getStringWidth(clipDestination) > spaceAvailable - 5) {
      clipDestination[strlen(clipDestination)-1] = '\0';
    }
    if (clipDestination[strlen(clipDestination)-1] == ' ') clipDestination[strlen(clipDestination)-1] = '\0';
    strcat(clipDestination,"...");
  }
  u8g2.drawStr(destPos,serviceBaseline,clipDestination);

  if (station.platformAvailable && !hidePlatform && station.service[serviceIndex].platform[0] && station.service[serviceIndex].serviceType == TRAIN) {
    sprintf(plat,"Plat %.3s",station.service[serviceIndex].platform);
    u8g2.drawStr(0,infoBaseline,plat);
  }
  if (isDigit(station.service[serviceIndex].etd[0])) sprintf(etd,"Exp %s",station.service[serviceIndex].etd);
  else strcpy(etd,station.service[serviceIndex].etd);
  int etdWidth = getStringWidth(etd) + (etd[strlen(etd)-1]=='1'?1:0);
  u8g2.drawStr(CYD_DETAIL_STATUS_RIGHT - etdWidth,infoBaseline,etd);
}

inline void drawSecondServiceLine() {
  drawSecondaryServiceLine(1);
}

void drawCydServicePanel(bool showVia) {
  blankArea(0,LINE1,SCREEN_WIDTH,LINE3-LINE1);
  if (isScrollingCydPrimaryService) drawCydPrimaryServiceTransition(showVia);
  else drawPrimaryService(showVia);
  if (station.numServices > 1) {
    if (cydSecondaryServiceIndex <= 0 || cydSecondaryServiceIndex >= station.numServices) {
      cydSecondaryServiceIndex = 1;
    }
    drawSecondaryServiceLine(cydSecondaryServiceIndex);
  }
}
#endif

// Draw the secondary service line
void drawServiceLine(int line, int y) {
#if defined(DISPLAY_CYD)
  uint8_t previousTextScale = u8g2.getTextScale();
  u8g2.setTextScale(1);
  const int detailBaseline = railDetailBaseline(y);
#else
  const int detailBaseline = railDetailBaseline(y);
#endif
  char clipDestination[MAXLOCATIONSIZE+5];
  char ordinal[8];
  char plat[9];
  int destPos;

  switch (line) {
    case 1:
      strcpy(ordinal,"2nd ");
      break;
    case 2:
      strcpy(ordinal,"3rd ");
      break;
    default:
      sprintf(ordinal,"%dth ",line+1);
      break;
  }

  setRailDetailFont();
  blankArea(0,y,256,9);

  if (line<station.numServices) {
    if (hideOrdinals) {
      destPos = u8g2.drawStr(0,detailBaseline,station.service[line].sTime) + 6;
    } else {
      int timeX = u8g2.drawStr(0,detailBaseline,ordinal) + 6;
      destPos = timeX + u8g2.drawStr(timeX,detailBaseline,station.service[line].sTime) + 6;
    }
    char etd[16];
    if (isDigit(station.service[line].etd[0])) sprintf(etd,"Exp %s",station.service[line].etd);
    else strcpy(etd,station.service[line].etd);
    int etdWidth = getStringWidth(etd) + (etd[strlen(etd)-1]=='1'?1:0);
    u8g2.drawStr(SCREEN_WIDTH - etdWidth,detailBaseline,etd);
    int spaceAvailable = SCREEN_WIDTH - destPos - etdWidth - 6;

    if (station.platformAvailable && !hidePlatform && station.service[line].platform[0] && station.service[line].serviceType == TRAIN) {
      sprintf(plat,"Plat %.3s",station.service[line].platform);
      int platWidth = getStringWidth(plat) + (plat[strlen(plat)-1]=='1'?1:0);
      u8g2.drawStr(SCREEN_WIDTH - etdWidth - platWidth - 7,detailBaseline,plat);
      spaceAvailable-=(platWidth+7);
    }
    // work out if we need to clip the destination
    strcpy(clipDestination,station.service[line].destination);
    if (station.service[line].serviceType == BUS) strcat(clipDestination," \x86"); // Add bus icon
    if (getStringWidth(clipDestination) > spaceAvailable) {
      while (getStringWidth(clipDestination) > spaceAvailable - 5) {
        clipDestination[strlen(clipDestination)-1] = '\0';
      }
      // check if there's a trailing space left
      if (clipDestination[strlen(clipDestination)-1] == ' ') clipDestination[strlen(clipDestination)-1] = '\0';
      strcat(clipDestination,"...");
    }
    u8g2.drawStr(destPos,detailBaseline,clipDestination);
  } else {
    if (weatherMsg[0] && line==station.numServices) {
      // We're showing the weather
      centreText(weatherMsg,detailBaseline);
    } else {
      // We're showing the mandatory attribution
      centreText(useRDMclient?rdgAttribution:nrAttributionn,detailBaseline);
    }
  }
#if defined(DISPLAY_CYD)
  u8g2.setTextScale(previousTextScale);
#endif
}

// Draw the initial Departures Board
void drawStationBoard() {
#if defined(DISPLAY_CYD)
  u8g2.setFontPosBaseline();
  const bool initialBoardLoad = firstLoad;
#endif
  if (showClockNoServices && station.numServices == 0) {
    if (!noServiceClockIsActive) firstLoad = true;
    noServiceClockIsActive = true;
  } else {
    if (noServiceClockIsActive) firstLoad = true;
    noServiceClockIsActive = false;
  }
  numMessages=0;
#if defined(DISPLAY_CYD)
  cydPrimaryMessageCount=0;
  cydPrimaryMessageIndex=0;
  displayedPrimaryServiceMessage[0]='\0';
  cydSecondaryServiceIndex=1;
  serviceTimer = millis() + 15000;
#endif
  if (firstLoad) {
    // Clear the entire screen for the first load since boot up/wake from sleep
    u8g2.clearBuffer();
    u8g2.setContrast(brightness);
    firstLoad=false;
    line3Service = noScrolling ? 1 : 0;
  } else {
    // Clear the top two lines
    blankArea(0,LINE0,256,LINE2-1);
  }

#if defined(DISPLAY_CYD)
  msgLine = LINE3;
#else
  msgLine = LINE2;
#endif
  msgMargin = 0;
  msgWidth = SCREEN_WIDTH;

  if (!noServiceClockIsActive) {
    drawStationHeader(station.location,callingStation,locationFilter,nrTimeOffset);

    // Draw the primary service line
    isShowingVia=false;
    viaTimer=millis()+300000;  // effectively don't check for via
    if (station.numServices) {
#if defined(DISPLAY_CYD)
      if (initialBoardLoad) beginCydPrimaryServiceAnimation(nullptr,false,false);
      drawCydServicePanel(false);
#else
      drawPrimaryService(false);
#endif
      if (station.service[0].via[0]) viaTimer=millis()+4000;
      if (station.service[0].isCancelled) {
        // This train is cancelled
        if (station.serviceMessage[0]) {
          strcpy(line2[0],station.serviceMessage);
          numMessages=1;
        }
      } else {
        // The train is not cancelled
#if defined(DISPLAY_CYD)
        if (station.serviceMessage[0]) {
          strcpy(line2[numMessages],station.serviceMessage);
          numMessages++;
        }
#else
        if (station.service[0].isDelayed && station.serviceMessage[0]) {
          // The train is delayed and there's a reason
          strcpy(line2[0],station.serviceMessage);
          numMessages++;
        }
#endif
        if (station.calling[0]) {
          // Add the calling stops message
          sprintf(line2[numMessages],"Calling at: %s",station.calling);
          numMessages++;
        }
        if (strcmp(station.origin, station.location)==0) {
          // Service originates at this station
          if (station.service[0].opco[0]) {
            sprintf(line2[numMessages],"This %s service starts here.",station.service[0].opco);
          } else {
            strcpy(line2[numMessages],"This service starts here.");
          }
          // Add the seating if available
          switch (station.service[0].classesAvailable) {
            case 1:
              strcat(line2[numMessages],firstClassSeating);
              break;
            case 2:
              strcat(line2[numMessages],standardClassSeating);
              break;
            case 3:
              strcat(line2[numMessages],dualClassSeating);
              break;
          }
          numMessages++;
        } else {
          // Service originates elsewhere
          strcpy(line2[numMessages],"");
          if (station.service[0].opco[0]) {
            if (station.origin[0]) {
              sprintf(line2[numMessages],"This is the %s service from %s.",station.service[0].opco,station.origin);
            } else {
              sprintf(line2[numMessages],"This is the %s service.",station.service[0].opco);
            }
          } else {
            if (station.origin[0]) {
              sprintf(line2[numMessages],"This service originated at %s.",station.origin);
            }
          }
          // Add the seating if available
          switch (station.service[0].classesAvailable) {
            case 1:
              strcat(line2[numMessages],firstClassSeating);
              break;
            case 2:
              strcat(line2[numMessages],standardClassSeating);
              break;
            case 3:
              strcat(line2[numMessages],dualClassSeating);
              break;
          }
          if (line2[numMessages][0]) numMessages++;
        }
        if (station.service[0].trainLength) {
          // Add the number of carriages message
          sprintf(line2[numMessages],"This train is formed of %d coaches.",station.service[0].trainLength);
          numMessages++;
        }
      }

#if defined(DISPLAY_CYD)
      for (int i=0;i<numMessages;i++) addCydPrimaryMessage(line2[i]);
      numMessages=0;
#endif

#if !defined(DISPLAY_CYD)
      if (noScrolling && station.numServices>1) {
        drawServiceLine(1,LINE2);
      }
#endif
    } else {
      blankArea(0,LINE2,256,LINE4-LINE2);
      u8g2.setFont(NatRailTall12);
      centreText("There are no scheduled services at this station.",LINE1-1);
    }
  } else {
    msgLine = LINE4;
    if (schedulerActive || carouselActive) {
      if (schedulerActive) {
        u8g2.drawStr(0,LINE4,"\x87");
        msgMargin = 11;
      } else if (carouselActive) {
        u8g2.drawStr(0,LINE4-1,"\x88");
        msgMargin = 12;
      }
      msgWidth = SCREEN_WIDTH - msgMargin;
    }
  }

  // Check if RSS should be inserted before nrcc messages
  if (rssEnabled && rssPriority && rssMessage[0]) {
    strcpy(line2[numMessages++],rssMessage);
  }

  // Add any nrcc messages
  for (int i=0;i<messages.numMessages;i++) {
#if defined(DISPLAY_CYD)
    addCydPrimaryMessage(messages.messages[i]);
#else
    strcpy(line2[numMessages],messages.messages[i]);
    numMessages++;
#endif
  }

  // Check if RSS should be added after nrcc messages
  if (rssEnabled && !rssPriority && rssMessage[0]) {
    strcpy(line2[numMessages++],rssMessage);
  }

#if defined(DISPLAY_CYD)
  if (weatherEnabled && weatherMsg[0]) {
    strcpy(line2[numMessages++],weatherMsg);
  }
#endif

  // Setup for the first message to rollover to
  isScrollingStops=false;
  currentMessage=numMessages-1;

#if defined(DISPLAY_CYD)
  u8g2.setTextScale(1);
  setRailDetailFont();
  u8g2.setMaxClipWindow();
  if (!noServiceClockIsActive && station.numServices) drawPrimaryServiceMessage();
  if (numMessages) {
    currentMessage=0;
    scrollStopsXpos=msgMargin;
    scrollStopsLength=getStringWidth(line2[currentMessage]);
    blankArea(msgMargin,msgLine,msgWidth,20);
    u8g2.setClipWindow(msgMargin,msgLine,SCREEN_WIDTH,msgLine+20);
    if (scrollStopsLength < msgWidth) {
      centreText(line2[currentMessage],railDetailBaseline(msgLine),msgMargin,msgWidth);
    } else {
      u8g2.drawStr(scrollStopsXpos,railDetailBaseline(msgLine),line2[currentMessage]);
    }
    u8g2.setMaxClipWindow();
    timer=millis()+6000;
  } else {
    blankArea(msgMargin,msgLine,msgWidth,20);
  }
  displayedTime[0] = '\0';
  drawCurrentTime();
  setRailDetailFont();
  u8g2.setFontPosBaseline();
#else
  setRailDetailFont();
#endif
  u8g2.sendBuffer();
}

void updateRailDepartures() {
  if (useRDMclient) rdmRailData.loadDepartures(&station,&messages);
  else darwinRailData.loadDepartures(&station,&messages);
  lastDataLoadTime = millis();
  noDataLoaded = false;
  dataLoadSuccess++;
}

void waitForFirstLoad() {
  // Wait for the first data load
  while (!fetchComplete) {
    delay(250);
    if (startupProgressPercent<95) {
      startupProgressPercent+=5;
      drawProgressBar(startupProgressPercent);
    }
  }
  drawProgressBar(100);
}

/*
 *
 * London Underground Board
 *
 */

#if !defined(DISPLAY_CYD)
// Draw the TfL clock (if the time has changed)
bool drawCurrentTimeUG() {
  if (strcmp(displayedTime,currentTime)) {
    u8g2.setFont(UndergroundClock8);
    blankArea(99,ULINE4,58,8);
    u8g2.drawStr(99,ULINE4-1,currentTime);
    u8g2.updateDisplayArea(12,7,8,1);
    strcpy(displayedTime,currentTime);
    u8g2.setFont(Underground10);

    if (dateEnabled && timeinfo.tm_mday!=dateDay) {
      if (boardMode == MODE_TUBE) drawStationHeader(locationName,"","",0);
      else drawStationHeader(locationName,"",locationFilter,0);
      u8g2.sendBuffer();  // Just refresh on new date
      u8g2.setFont(Underground10);
    }
    return true;
  } else {
    return false;
  }
}
#endif

void updateArrivals() {
  tfldata.loadArrivals(&station,&messages);
  lastDataLoadTime = millis();
  noDataLoaded = false;
  dataLoadSuccess++;
#if defined(DISPLAY_CYD)
  numMessages = 0;
  if (weatherEnabled && weatherMsg[0]) {
    strcpy(line2[numMessages++],weatherMsg);
  }
  if (rssEnabled && rssPriority && rssMessage[0] && !noScrolling) {
    strcpy(line2[numMessages++],rssMessage);
  }
  for (int i=0;i<messages.numMessages;i++) {
    strcpy(line2[numMessages++],messages.messages[i]);
  }
  if (rssEnabled && !rssPriority && rssMessage[0] && !noScrolling) {
    strcpy(line2[numMessages++],rssMessage);
  }
#endif
}

#if defined(DISPLAY_CYD)
void drawUndergroundService(int serviceId, int y, bool isShowingCurrentLocation = false, bool blank = true) {
  char clipDestination[MAXLOCATIONSIZE];
  char etd[16] = "";

  if (serviceId < station.numServices) {
    u8g2.setTextScale(1);
    setRailDetailFont();
    if (blank) blankArea(0,y,SCREEN_WIDTH,22);
    const int baseline = railDetailBaseline(y);

    char prefix[8];
    sprintf(prefix,"%d ",serviceId+1);
    int destPos = u8g2.drawStr(0,baseline,prefix);

    if (serviceId || (strcmp(station.origin,"At Platform") && station.service[0].timeToStation>10)) {
      if (station.service[serviceId].timeToStation <= 40) {
        strcpy(etd,"Due");
      } else {
        int mins = (station.service[serviceId].timeToStation + 30) / 60;
        sprintf(etd,"%d %s",mins,(mins==1)?"min":"mins");
      }
    }
    int etdWidth = etd[0] ? getStringWidth(etd) + (etd[strlen(etd)-1]=='1'?1:0) : 0;
    if (etd[0]) {
      u8g2.drawStr(CYD_DETAIL_STATUS_RIGHT - etdWidth,baseline,etd);
    }

    const char *text = (isShowingCurrentLocation && station.origin[0]) ? station.origin : station.service[serviceId].destination;
    strlcpy(clipDestination,text,sizeof(clipDestination));

    int spaceAvailable = CYD_DETAIL_STATUS_RIGHT - destPos - etdWidth - 8;
    if (spaceAvailable > 0 && getStringWidth(clipDestination) > spaceAvailable) {
      while (getStringWidth(clipDestination) > spaceAvailable - 8 && strlen(clipDestination) > 0) {
        clipDestination[strlen(clipDestination)-1] = '\0';
      }
      if (clipDestination[strlen(clipDestination)-1] == ' ') clipDestination[strlen(clipDestination)-1] = '\0';
      strcat(clipDestination,"...");
    }
    if (spaceAvailable > 0) {
      u8g2.drawStr(destPos,baseline,clipDestination);
    }
  }
}

// Draw/update the Underground Arrivals Board
void drawUndergroundBoard() {
  u8g2.setFontPosBaseline();
  if (firstLoad) {
    u8g2.clearBuffer();
    u8g2.setContrast(brightness);
    firstLoad = false;
    cydSecondaryServiceIndex = 2;
    serviceTimer = millis() + 10000;
  } else {
    blankArea(0,0,SCREEN_WIDTH,LINE4);
  }
  drawStationHeader(locationName,"","",0);

  isShowingVia = false;
  if (station.origin[0]) viaTimer = millis() + 6000; else viaTimer = millis() + 300000;

  u8g2.setTextScale(1);
  setRailDetailFont();

  if (station.numServices == 0) {
    centreText("There are no scheduled arrivals at this station.",railDetailBaseline(90));
  } else {
    drawUndergroundService(0,68,false);
    if (station.numServices > 1) drawUndergroundService(1,102,false);
    if (station.numServices > 2) {
      if (cydSecondaryServiceIndex < 2 || cydSecondaryServiceIndex >= station.numServices) {
        cydSecondaryServiceIndex = 2;
      }
      drawUndergroundService(cydSecondaryServiceIndex,136,false);
    }
  }

  // Populate line2 messages for bottom ticker
  numMessages = 0;
  if (weatherEnabled && weatherMsg[0]) {
    strcpy(line2[numMessages++],weatherMsg);
  }
  if (rssEnabled && rssPriority && rssMessage[0] && !noScrolling) {
    strcpy(line2[numMessages++],rssMessage);
  }
  for (int i=0; i<messages.numMessages; i++) {
    strcpy(line2[numMessages++],messages.messages[i]);
  }
  if (rssEnabled && !rssPriority && rssMessage[0] && !noScrolling) {
    strcpy(line2[numMessages++],rssMessage);
  }

  // Draw bottom ticker
  if (numMessages > 0) {
    currentMessage = 0;
    scrollStopsXpos = 0;
    scrollStopsLength = getStringWidth(line2[currentMessage]);
    blankArea(0,LINE3,SCREEN_WIDTH,20);
    u8g2.setClipWindow(0,LINE3,SCREEN_WIDTH,LINE3+20);
    if (scrollStopsLength <= SCREEN_WIDTH) {
      centreText(line2[currentMessage],railDetailBaseline(LINE3));
    } else {
      u8g2.drawStr(scrollStopsXpos,railDetailBaseline(LINE3),line2[currentMessage]);
    }
    u8g2.setMaxClipWindow();
    timer = millis() + 6000;
  } else {
    blankArea(0,LINE3,SCREEN_WIDTH,20);
  }

  displayedTime[0] = '\0';
  drawCurrentTime();
  setRailDetailFont();
  u8g2.setFontPosBaseline();

  u8g2.sendBuffer();
}
#else
void drawUndergroundService(int serviceId, int y, bool isShowingCurrentLocation = false) {
  char serviceData[4+MAXLOCATIONSIZE];
  int usedSpace = 4;

  u8g2.setFont(Underground10);
  blankArea(0,y,256,10);

  if (serviceId < station.numServices) {
    if (serviceId || (strcmp(station.origin,"At Platform") && station.service[0].timeToStation>10)) {
      if (station.service[serviceId].timeToStation <= 40) {
        usedSpace += u8g2.drawStr(SCREEN_WIDTH-19,y-1,"Due");
      } else {
        int mins = (station.service[serviceId].timeToStation + 30) / 60; // Round to nearest minute
        sprintf(serviceData,"%d",mins);
        if (mins==1) u8g2.drawStr(SCREEN_WIDTH-22,y-1,"min"); else u8g2.drawStr(SCREEN_WIDTH-22,y-1,"mins");
        usedSpace += u8g2.drawStr(SCREEN_WIDTH-27-(strlen(serviceData)*7),y-1,serviceData) + 22;
      }
    }

    if (isShowingCurrentLocation) sprintf(serviceData,"%d %s",serviceId+1,station.origin);
    else sprintf(serviceData,"%d %s",serviceId+1,station.service[serviceId].destination);
    if (getStringWidth(serviceData) > SCREEN_WIDTH-usedSpace) {
      while (getStringWidth(serviceData) > SCREEN_WIDTH-usedSpace-6) {
        serviceData[strlen(serviceData)-1] = '\0';
      }
      if (serviceData[strlen(serviceData)-1] == ' ') serviceData[strlen(serviceData)-1] = '\0'; // remove any trailing space
      strcat(serviceData,"\x81");
    }
    u8g2.drawStr(0,y-1,serviceData);
  }
}

// Draw/update the Underground Arrivals Board
void drawUndergroundBoard() {
  if (line3Service==0) line3Service=1;
  attributionScrolled=false;
  if (firstLoad) {
    // Clear the entire screen for the first load since boot up/wake from sleep
    u8g2.clearBuffer();
    u8g2.setContrast(brightness);
    firstLoad=false;
  } else {
      // Clear the top three lines
      blankArea(0,ULINE0,256,ULINE3-1);
  }
  drawStationHeader(locationName,"","",0);

  if (station.boardChanged) {
    isShowingVia = false;
    if (station.origin[0]) viaTimer=millis()+6000; else viaTimer=millis()+300000;
    // prepare to scroll up primary services
    scrollPrimaryYpos = 11;
    isScrollingPrimary = true;
    // reset line3
    line3Service = 99;
    prevScrollStopsLength = 0;
    currentMessage=99;
    blankArea(0,ULINE3,256,11);
    serviceTimer=0;
  } else {
    // Draw the primary service line(s)
    if (station.numServices) {
      drawUndergroundService(0,ULINE1);
      if (station.numServices>1) drawUndergroundService(1,ULINE2);
    } else {
      u8g2.setFont(Underground10);
      centreText("There are no scheduled arrivals at this station.",ULINE1-1);
    }
  }

  numMessages = 0;

  // Add weather message if enabled and available
  if (weatherEnabled && weatherMsg[0]) {
    strcpy(line2[numMessages],weatherMsg);
    numMessages++;
  }

  // Check if RSS should be inserted before TfL messages
  if (rssEnabled && rssPriority && rssMessage[0] && !noScrolling) {
    strcpy(line2[numMessages],rssMessage);
    numMessages++;
  }

  // Add any TfL messages
  for (int i=0;i<messages.numMessages;i++) {
    strcpy(line2[numMessages],messages.messages[i]);
    numMessages++;
  }

  // Check if RSS should be added after TfL messages
  if (rssEnabled && !rssPriority && rssMessage[0] && !noScrolling) {
    strcpy(line2[numMessages],rssMessage);
    numMessages++;
  }

  u8g2.sendBuffer();
}
#endif

/*
 *
 * Bus Departures Board
 *
 */
#if defined(DISPLAY_CYD)
void drawBusService(int serviceId, int y, int destPos, bool blank = true) {
  char clipDestination[MAXLOCATIONSIZE];
  char etd[16];

  if (serviceId < station.numServices) {
    u8g2.setTextScale(1);
    setRailDetailFont();
    if (blank) blankArea(0,y,SCREEN_WIDTH,22);
    const int baseline = railDetailBaseline(y);

    u8g2.drawStr(0,baseline,station.service[serviceId].via);
    if (isDigit(station.service[serviceId].etd[0])) {
      sprintf(etd,"Exp %s",station.service[serviceId].etd);
    } else if (station.service[serviceId].etd[0]) {
      strcpy(etd,station.service[serviceId].etd);
    } else {
      strcpy(etd,station.service[serviceId].sTime);
    }
int etdWidth = etd[0] ? getStringWidth(etd) + (etd[strlen(etd)-1]=='1'?1:0) : 0;
    u8g2.drawStr(CYD_DETAIL_STATUS_RIGHT - etdWidth,baseline,etd);

    // work out if we need to clip the destination
    strcpy(clipDestination,station.service[serviceId].destination);
    int spaceAvailable = CYD_DETAIL_STATUS_RIGHT - destPos - etdWidth - 8;
    if (spaceAvailable > 0 && getStringWidth(clipDestination) > spaceAvailable) {
      while (getStringWidth(clipDestination) > spaceAvailable - 8 && strlen(clipDestination) > 0) {
        clipDestination[strlen(clipDestination)-1] = '\0';
      }
      if (clipDestination[strlen(clipDestination)-1] == ' ') clipDestination[strlen(clipDestination)-1] = '\0';
      strcat(clipDestination,"...");
    }
    if (spaceAvailable > 0) {
      u8g2.drawStr(destPos,baseline,clipDestination);
    }
  }
}

// Draw/update the Bus Departures Board
void drawBusDeparturesBoard() {
  u8g2.setFontPosBaseline();
  if (firstLoad) {
    u8g2.clearBuffer();
    u8g2.setContrast(brightness);
    firstLoad=false;
    cydSecondaryServiceIndex = 2;
    serviceTimer = millis() + 10000;
  } else {
    blankArea(0,0,SCREEN_WIDTH,LINE4);
  }
  drawStationHeader(locationName,"",locationFilter,0);

  u8g2.setTextScale(1);
  setRailDetailFont();

  if (station.numServices == 0) {
    centreText("There are no scheduled services at this stop.",railDetailBaseline(90));
  } else {
    drawBusService(0,68,busDestX);
    if (station.numServices > 1) drawBusService(1,102,busDestX);
    if (station.numServices > 2) {
      if (cydSecondaryServiceIndex < 2 || cydSecondaryServiceIndex >= station.numServices) {
        cydSecondaryServiceIndex = 2;
      }
      drawBusService(cydSecondaryServiceIndex,136,busDestX);
    }
  }

  // Draw bottom ticker attribution / weather
  if (messages.numMessages > 0) {
    currentMessage = 0;
    scrollStopsXpos = 0;
    scrollStopsLength = getStringWidth(line2[currentMessage]);
    blankArea(0,LINE3,SCREEN_WIDTH,20);
    u8g2.setClipWindow(0,LINE3,SCREEN_WIDTH,LINE3+20);
    if (scrollStopsLength <= SCREEN_WIDTH) {
      centreText(line2[currentMessage],railDetailBaseline(LINE3));
    } else {
      u8g2.drawStr(scrollStopsXpos,railDetailBaseline(LINE3),line2[currentMessage]);
    }
    u8g2.setMaxClipWindow();
    timer = millis() + 6000;
  }

  displayedTime[0] = '\0';
  drawCurrentTime();
  setRailDetailFont();
  u8g2.setFontPosBaseline();

  u8g2.sendBuffer();
}
#else
void drawBusService(int serviceId, int y, int destPos) {
  char clipDestination[MAXLOCATIONSIZE];
  char etd[16];

  if (serviceId < station.numServices) {
    u8g2.setFont(NatRailSmall9);
    blankArea(0,y,256,9);

    u8g2.drawStr(0,y-1,station.service[serviceId].via);
    int etdWidth = 25;
    if (isDigit(station.service[serviceId].etd[0])) {
      sprintf(etd,"Exp %s",station.service[serviceId].etd);
      etdWidth = 47;
    } else strcpy(etd,station.service[serviceId].sTime);
    u8g2.drawStr(SCREEN_WIDTH - etdWidth,y-1,etd);

    // work out if we need to clip the destination
    strcpy(clipDestination,station.service[serviceId].destination);
    int spaceAvailable = SCREEN_WIDTH - destPos - etdWidth - 6;
    if (getStringWidth(clipDestination) > spaceAvailable) {
      while (getStringWidth(clipDestination) > spaceAvailable - 17) {
        clipDestination[strlen(clipDestination)-1] = '\0';
      }
      // check if there's a trailing space left
      if (clipDestination[strlen(clipDestination)-1] == ' ') clipDestination[strlen(clipDestination)-1] = '\0';
      strcat(clipDestination,"...");
    }
    u8g2.drawStr(destPos,y-1,clipDestination);
  }
}

// Draw/update the Bus Departures Board
void drawBusDeparturesBoard() {

  if (line3Service==0) line3Service=1;
  if (firstLoad) {
    // Clear the entire screen for the first load since boot up/wake from sleep
    u8g2.clearBuffer();
    u8g2.setContrast(brightness);
    firstLoad=false;
  } else {
      // Clear the top three lines
      blankArea(0,ULINE0,256,ULINE3-1);
  }
  drawStationHeader(locationName,"",locationFilter,0);

  if (station.boardChanged) {
    // prepare to scroll up primary services
    scrollPrimaryYpos = 11;
    isScrollingPrimary = true;
    // reset line3
    if (station.numServices>2) {
      line3Service=2;
    } else {
      line3Service=99;
    }
    currentMessage = -1;
    blankArea(0,ULINE3,256,11);
    serviceTimer=0;
  } else {
    // Draw the primary service line(s)
    if (station.numServices) {
      drawBusService(0,ULINE1,busDestX);
      if (station.numServices>1) drawBusService(1,ULINE2,busDestX);
    } else {
      u8g2.setFont(NatRailSmall9);
      centreText("There are no scheduled services at this stop.",ULINE1-1);
    }
  }
  u8g2.sendBuffer();
}
#endif

void updateBusDepartures() {
  busdata.loadDepartures(&station);
  lastDataLoadTime = millis();
  noDataLoaded = false;
  dataLoadSuccess++;
  // Work out the max column size for service numbers
  busDestX=0;
#if defined(DISPLAY_CYD)
  u8g2.setTextScale(1);
  setRailDetailFont();
#else
  u8g2.setFont(NatRailSmall9);
#endif
  for (int i=0;i<station.numServices;i++) {
    int svcWidth = getStringWidth(station.service[i].via);
    busDestX = (busDestX > svcWidth) ? busDestX : svcWidth;
  }
#if defined(DISPLAY_CYD)
  busDestX += 10;
#else
  busDestX += 5;
#endif
  if (weatherEnabled && weatherMsg[0]) {
    strcpy(line2[0],weatherMsg);
    strcpy(line2[1],btAttribution);
    messages.numMessages=2;
  } else{
    strcpy(line2[0],btAttribution);
    messages.numMessages=1;
  }
}

/*
 * Web GUI functions
 */

// Helper function for returning text status messages
void sendResponse(int code, String msg, AsyncWebServerRequest *request) {
  request->send(code,contentTypeText,msg);
}

// Return the correct MIME type for a file name
String getContentType(String filename) {
  if (filename.endsWith(".htm")) {
    return "text/html";
  } else if (filename.endsWith(".html")) {
    return "text/html";
  } else if (filename.endsWith(".css")) {
    return "text/css";
  } else if (filename.endsWith(".js")) {
    return "application/javascript";
  } else if (filename.endsWith(".png")) {
    return "image/png";
  } else if (filename.endsWith(".gif")) {
    return "image/gif";
  } else if (filename.endsWith(".jpg")) {
    return "image/jpeg";
  } else if (filename.endsWith(".ico")) {
    return "image/x-icon";
  } else if (filename.endsWith(".xml")) {
    return "text/xml";
  } else if (filename.endsWith(".pdf")) {
    return "application/x-pdf";
  } else if (filename.endsWith(".zip")) {
    return "application/x-zip";
  } else if (filename.endsWith(".json")) {
    return "application/json";
  } else if (filename.endsWith(".gz")) {
    return "application/x-gzip";
  } else if (filename.endsWith(".svg")) {
    return "image/svg+xml";
  } else if (filename.endsWith(".webp")) {
    return "image/webp";
  }
  return "text/plain";
}

// Stream a file from the file system
bool handleStreamFile(String filename, AsyncWebServerRequest *request) {
  if (LittleFS.exists(filename)) {
    String contentType = getContentType(filename);
    request->send(LittleFS,filename,contentType);
    return true;
  } else return false;
}

// Stream a file stored in flash (default graphics are now included in the firmware image)
void handleStreamFlashFile(String filename, const uint8_t *filedata, size_t contentLength, AsyncWebServerRequest *request) {
  String contentType = getContentType(filename);
  AsyncWebServerResponse *response = request->beginResponse(200, contentType, filedata, contentLength);
  response->addHeader("Cache-Control", "public,max-age=3600,s-maxage=3600");
  request->send(response);
}

void handleStreamGzipFlashFile(String filename, const uint8_t *filedata, size_t contentLength, AsyncWebServerRequest *request) {
  String contentType = getContentType(filename);
  AsyncWebServerResponse *response = request->beginResponse(200, contentType, filedata, contentLength);
  response->addHeader("Content-Encoding", "gzip");
  request->send(response);
}

// Screenshot capture in BMP format from the U8g2 display buffer
void handleScreenshot(AsyncWebServerRequest *request) {
#if defined(DISPLAY_CYD)
  const uint16_t width = CYD_NATIVE_WIDTH;
  const uint16_t height = CYD_NATIVE_HEIGHT;
  const uint8_t tile_width = CYD_NATIVE_TILE_WIDTH;
  uint8_t fgR = ((cyd_fg_color >> 11) & 0x1F) * 255 / 31;
  uint8_t fgG = ((cyd_fg_color >> 5) & 0x3F) * 255 / 63;
  uint8_t fgB = (cyd_fg_color & 0x1F) * 255 / 31;
#else
  const uint16_t width = 256;
  const uint16_t height = 64;
  const uint8_t tile_width = 32;
  uint8_t fgR = 255;
  uint8_t fgG = 176;
  uint8_t fgB = 0;
#endif

  const uint16_t row_bytes = width / 8;
  const uint32_t fileSize = 62 + (uint32_t)row_bytes * height;
  const uint8_t *u8g2_buf = u8g2.getBufferPtr();

  std::array<uint8_t, 62> header = {};
  header[0] = 'B';
  header[1] = 'M';
  header[2] = (uint8_t)(fileSize);
  header[3] = (uint8_t)(fileSize >> 8);
  header[4] = (uint8_t)(fileSize >> 16);
  header[5] = (uint8_t)(fileSize >> 24);
  header[10] = 62; // bfOffBits

  header[14] = 40; // biSize
  header[18] = (uint8_t)(width);
  header[19] = (uint8_t)(width >> 8);
  header[22] = (uint8_t)(height);
  header[23] = (uint8_t)(height >> 8);
  header[26] = 1;  // biPlanes
  header[28] = 1;  // biBitCount (1-bit monochrome/indexed)
  uint32_t imageSize = (uint32_t)row_bytes * height;
  header[34] = (uint8_t)(imageSize);
  header[35] = (uint8_t)(imageSize >> 8);
  header[36] = (uint8_t)(imageSize >> 16);
  header[37] = (uint8_t)(imageSize >> 24);
  header[38] = 0x13; header[39] = 0x0B; // biXPelsPerMeter = 2835
  header[42] = 0x13; header[43] = 0x0B; // biYPelsPerMeter = 2835
  header[46] = 2;  // biClrUsed
  header[50] = 2;  // biClrImportant

  // Palette 0: Black (0, 0, 0, 0)
  // Palette 1: Foreground (B, G, R, 0)
  header[58] = fgB;
  header[59] = fgG;
  header[60] = fgR;
  header[61] = 0;

  AsyncWebServerResponse *response = request->beginResponse("image/bmp", fileSize,
    [header, u8g2_buf, width, height, tile_width, row_bytes, fileSize](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      size_t written = 0;
      while (written < maxLen && (index + written) < fileSize) {
        size_t pos = index + written;
        if (pos < 62) {
          buffer[written] = header[pos];
        } else {
          size_t pixel_index = pos - 62;
          size_t row = pixel_index / row_bytes;
          size_t bx = pixel_index % row_bytes;
          int y = (height - 1) - row;
          uint8_t tile_y = y / 8;
          uint8_t vy = y % 8;
          uint8_t bit_mask = 1 << vy;
          const uint8_t *tptr = u8g2_buf + (tile_y * tile_width * 8) + (bx * 8);
          uint8_t bmp_byte = 0;
          for (uint8_t sx = 0; sx < 8; sx++) {
            if (tptr[sx] & bit_mask) {
              bmp_byte |= (0x80 >> sx);
            }
          }
          buffer[written] = bmp_byte;
        }
        written++;
      }
      return written;
    }
  );
  response->addHeader("Content-Disposition", "inline; filename=\"screenshot.bmp\"");
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  request->send(response);
}

// Dump screenshot to Serial console as hex stream
void dumpSerialScreenshot() {
#if defined(DISPLAY_CYD)
  const uint16_t width = CYD_NATIVE_WIDTH;
  const uint16_t height = CYD_NATIVE_HEIGHT;
  const uint8_t tile_width = CYD_NATIVE_TILE_WIDTH;
  uint8_t fgR = ((cyd_fg_color >> 11) & 0x1F) * 255 / 31;
  uint8_t fgG = ((cyd_fg_color >> 5) & 0x3F) * 255 / 63;
  uint8_t fgB = (cyd_fg_color & 0x1F) * 255 / 31;
#else
  const uint16_t width = 256;
  const uint16_t height = 64;
  const uint8_t tile_width = 32;
  uint8_t fgR = 255;
  uint8_t fgG = 176;
  uint8_t fgB = 0;
#endif

  const uint16_t row_bytes = width / 8;
  const uint32_t fileSize = 62 + (uint32_t)row_bytes * height;
  const uint8_t *u8g2_buf = u8g2.getBufferPtr();

  Serial.printf("\n--- SNAPSHOT START (%ux%u BMP, %u bytes) ---\n", width, height, fileSize);
  if (wifiConnected) {
    Serial.printf("Direct download: %s/screenshot.bmp\n", myUrl);
  }

  uint8_t header[62] = {0};
  header[0] = 'B'; header[1] = 'M';
  header[2] = (uint8_t)(fileSize); header[3] = (uint8_t)(fileSize >> 8);
  header[4] = (uint8_t)(fileSize >> 16); header[5] = (uint8_t)(fileSize >> 24);
  header[10] = 62;
  header[14] = 40;
  header[18] = (uint8_t)(width); header[19] = (uint8_t)(width >> 8);
  header[22] = (uint8_t)(height); header[23] = (uint8_t)(height >> 8);
  header[26] = 1; header[28] = 1;
  uint32_t imageSize = (uint32_t)row_bytes * height;
  header[34] = (uint8_t)(imageSize); header[35] = (uint8_t)(imageSize >> 8);
  header[36] = (uint8_t)(imageSize >> 16); header[37] = (uint8_t)(imageSize >> 24);
  header[38] = 0x13; header[39] = 0x0B;
  header[42] = 0x13; header[43] = 0x0B;
  header[46] = 2; header[50] = 2;
  header[58] = fgB; header[59] = fgG; header[60] = fgR;

  for (size_t i = 0; i < 62; i++) {
    Serial.printf("%02x", header[i]);
  }
  for (int y = height - 1; y >= 0; y--) {
    uint8_t tile_y = y / 8;
    uint8_t vy = y % 8;
    uint8_t bit_mask = 1 << vy;
    for (uint8_t bx = 0; bx < tile_width; bx++) {
      const uint8_t *tptr = u8g2_buf + (tile_y * tile_width * 8) + (bx * 8);
      uint8_t bmp_byte = 0;
      for (uint8_t sx = 0; sx < 8; sx++) {
        if (tptr[sx] & bit_mask) bmp_byte |= (0x80 >> sx);
      }
      Serial.printf("%02x", bmp_byte);
    }
  }
  Serial.println("\n--- SNAPSHOT END ---");
}

/*
 * Expose the file system via the Web GUI with some basic functions for directory browsing, file reading and deletion.
 */

// Return storage information
String getFSInfo() {
  char info[70];

  sprintf(info,"Total: %d bytes, Used: %d bytes\n",LittleFS.totalBytes(), LittleFS.usedBytes());
  return String(info);
}

// Send a basic directory listing to the browser
void handleFileList(AsyncWebServerRequest *request) {
  String path;
  if (!request->hasParam("dir")) path="/"; else path = request->getParam("dir")->value();
  File root = LittleFS.open(path);

  String output="<html><body style=\"font-family:Helvetica,Arial,sans-serif\"><h2>Departures Board File System</h2>";
  if (!root) {
    output+="<p>Failed to open directory</p>";
  } else if (!root.isDirectory()) {
    output+="<p>Not a directory</p>";
  } else {
    output+="<table>";
    File file = root.openNextFile();
    while (file) {
      output+="<tr><td>";
      if (file.isDirectory()) {
        output+="[DIR]</td><td><a href=\"/rmdir?f=" + String(file.path()) + "\" title=\"Delete\">X</a></td><td><a href=\"/dir?dir=" + String(file.path()) + "\">" + String(file.name()) + "</a></td></tr>";
      } else {
        output+=String(file.size()) + "</td><td><a href=\"/del?f="+ String(file.path()) + "\" title=\"Delete\">X</a></td><td><a href=\"/cat?f=" + String(file.path()) + "\">" + String(file.name()) + "</a></td></tr>";
      }
      file = root.openNextFile();
    }
  }

  output += "</table><br>";
  output += getFSInfo() + "<p><a href=\"/upload\">Upload</a> a file</p></body></html>";
  request->send(200,contentTypeHtml,output);
}

// Stream a file to the browser
void handleCat(AsyncWebServerRequest *request) {
  if (request->hasParam("f")) {
    String filename = request->getParam("f")->value();
    handleStreamFile(filename,request);
  } else sendResponse(404,"Not found",request);
}

// Delete a file from the file system
void handleDelete(AsyncWebServerRequest *request) {
  if (request->hasParam("f")) {
    String filename = request->getParam("f")->value();
    if (LittleFS.remove(filename)) {
      // Successfully removed go back to directory listing
      request->redirect("/dir");
    } else sendResponse(400,"Failed to delete file",request);
  } else sendResponse(404,"Not found",request);
}

// Format the file system
void handleFormatFFS(AsyncWebServerRequest *request) {
  String message;

  if (LittleFS.format()) {
    message="File System was successfully formatted\n\n";
    message+=getFSInfo();
  } else message="File System could not be formatted!";
  sendResponse(200,message,request);
}

/*
 * Web GUI handlers
 */

// Fallback function for browser requests
void handleNotFound(AsyncWebServerRequest *request) {
  if ((LittleFS.exists(request->url())) && (request->method() == HTTP_GET)) handleStreamFile(request->url(),request);
  else if (request->url() == "/keys.htm") handleStreamGzipFlashFile(request->url(), keyshtm, sizeof(keyshtm),request);
  else if (request->url() == "/index.htm") handleStreamGzipFlashFile(request->url(), indexhtm, sizeof(indexhtm),request);
  else if (request->url() == "/editrss.htm") handleStreamGzipFlashFile(request->url(), editrsshtm, sizeof(editrsshtm),request);
  else if (request->url() == "/nrelogo.webp") handleStreamFlashFile(request->url(), nrelogo, sizeof(nrelogo),request);
  else if (request->url() == "/rdglogo.webp") handleStreamFlashFile(request->url(), rdglogo, sizeof(nrelogo),request);
  else if (request->url() == "/tfllogo.webp") handleStreamFlashFile(request->url(), tfllogo, sizeof(tfllogo),request);
  else if (request->url() == "/btlogo.webp") handleStreamFlashFile(request->url(), btlogo, sizeof(btlogo),request);
  else if (request->url() == "/tube.webp") handleStreamFlashFile(request->url(), tubeicon, sizeof(tubeicon),request);
  else if (request->url() == "/nr.webp") handleStreamFlashFile(request->url(), nricon, sizeof(nricon),request);
  else if (request->url() == "/ibus.webp") handleStreamFlashFile(request->url(), ibus, sizeof(ibus),request);
  else if (request->url() == "/irail.webp") handleStreamFlashFile(request->url(), irail, sizeof(irail),request);
  else if (request->url() == "/itube.webp") handleStreamFlashFile(request->url(), itube, sizeof(itube),request);
  else if (request->url() == "/favicon.png") handleStreamFlashFile(request->url(), faviconpng, sizeof(faviconpng),request);
  else if (request->url() == "/rss.json") handleStreamGzipFlashFile(request->url(), rssjson, sizeof(rssjson),request);
  else sendResponse(404,"Not Found",request);
}

String getResultCodeText(int resultCode) {
  switch (resultCode) {
    case UPD_SUCCESS:
      return "SUCCESS";
      break;
    case UPD_NO_CHANGE:
      return "SUCCESS (NO CHANGES)";
      break;
    case UPD_SEC_CHANGE:
      return "SUCCESS (SECONDARY CHANGES)";
      break;
    case UPD_DATA_ERROR:
      return "DATA ERROR";
      break;
    case UPD_UNAUTHORISED:
      return "UNAUTHORISED";
      break;
    case UPD_HTTP_ERROR:
      return "HTTP ERROR";
      break;
    case UPD_INCOMPLETE:
      return "INCOMPLETE DATA RECEIVED";
      break;
    case UPD_NO_RESPONSE:
      return "NO RESPONSE FROM SERVER";
      break;
    case UPD_TIMEOUT:
      return "TIMEOUT WAITING FOR SERVER";
      break;
    default:
      return "OTHER ERROR";
      break;
  }
}

// JSON-escape a bounded string; bytes that are not valid UTF-8 are emitted as Latin-1 code points.
static void writeJsonString(Print &out, const char *s, size_t maxLen) {
  out.write('"');
  const size_t len = strnlen(s, maxLen);
  for (size_t i = 0; i < len; i++) {
    const uint8_t c = (uint8_t)s[i];
    if (c == '"' || c == '\\') {
      out.write('\\');
      out.write(c);
    } else if (c < 0x20 || c == 0x7F) {
      out.printf("\\u%04x", c);
    } else if (c < 0x80) {
      out.write(c);
    } else {
      size_t n = ((c & 0xE0) == 0xC0) ? 2 : ((c & 0xF0) == 0xE0) ? 3 : ((c & 0xF8) == 0xF0) ? 4 : 0;
      bool valid = n && c >= 0xC2 && c <= 0xF4 && i + n <= len;
      for (size_t j = 1; valid && j < n; j++) valid = ((uint8_t)s[i + j] & 0xC0) == 0x80;
      if (valid) {
        const uint8_t c1 = (uint8_t)s[i + 1];
        if ((c == 0xE0 && c1 < 0xA0) || (c == 0xED && c1 >= 0xA0) || (c == 0xF0 && c1 < 0x90) || (c == 0xF4 && c1 >= 0x90)) valid = false;
      }
      if (valid) {
        out.write((const uint8_t *)s + i, n);
        i += n - 1;
      } else {
        out.printf("\\u%04x", c);
      }
    }
  }
  out.write('"');
}

static void writeJsonField(Print &out, const char *key, const char *value, size_t maxLen) {
  out.printf(",\"%s\":", key);
  writeJsonString(out, value, maxLen);
}

// Public JSON snapshot of the active board for the responsive web view
void handleDeparturesJson(AsyncWebServerRequest *request) {
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 20000) {
    AsyncWebServerResponse *busy = request->beginResponse(503, contentTypeJson, "{\"error\":\"busy\"}");
    busy->addHeader("Retry-After", "10");
    busy->addHeader("Cache-Control", "no-store");
    busy->addHeader("X-Content-Type-Options", "nosniff");
    request->send(busy);
    return;
  }

  AsyncResponseStream *out = request->beginResponseStream(contentTypeJson, 6144);
  out->addHeader("Cache-Control", "no-store");
  out->addHeader("X-Content-Type-Options", "nosniff");

  const boardModes mode = boardMode;
  const bool validMode = (mode == MODE_RAIL || mode == MODE_TUBE || mode == MODE_BUS);
  const bool hasData = validMode && !noDataLoaded;
  const char *modeName = (mode == MODE_TUBE) ? "tube" : (mode == MODE_BUS) ? "bus" : "rail";

  struct tm now;
  const bool timeValid = getLocalTime(&now, 0) && now.tm_year > 120;
  char timeStr[9] = "";
  char dateStr[11] = "";
  if (timeValid) {
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &now);
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &now);
  }

  const int result = lastUpdateResult;
  out->printf("{\"mode\":\"%s\"", modeName);
  const bool railName = (mode == MODE_RAIL) && hasData && station.location[0];
  writeJsonField(*out, "location", railName ? station.location : locationName, MAXLOCATIONSIZE);
  writeJsonField(*out, "filter", (mode == MODE_TUBE) ? "" : locationFilter, sizeof(locationFilter));
  writeJsonField(*out, "callingFilter", (mode == MODE_RAIL) ? callingStation : "", sizeof(callingStation));
  out->printf(",\"hasData\":%s,\"sleeping\":%s,\"timeValid\":%s", hasData ? "true" : "false", isSleeping ? "true" : "false", timeValid ? "true" : "false");
  writeJsonField(*out, "time", timeStr, sizeof(timeStr));
  writeJsonField(*out, "date", dateStr, sizeof(dateStr));
  if (hasData) out->printf(",\"ageSec\":%lu", (millis() - lastDataLoadTime) / 1000UL);
  else out->print(",\"ageSec\":null");
  out->printf(",\"resultCode\":%d", result);
  writeJsonField(*out, "result", getResultCodeText(result).c_str(), MAXRESULTMESSAGESIZE);

  const bool showPlatforms = (mode == MODE_RAIL) && station.platformAvailable && !hidePlatform;
  out->printf(",\"platforms\":%s,\"services\":[", showPlatforms ? "true" : "false");
  const int numServices = hasData ? constrain(station.numServices, 0, MAXBOARDSERVICES) : 0;
  for (int i = 0; i < numServices; i++) {
    rdService s;
    memcpy(&s, &station.service[i], sizeof(s));
    out->print(i ? ",{" : "{");
    if (mode == MODE_RAIL) {
      out->print("\"time\":");
      writeJsonString(*out, s.sTime, sizeof(s.sTime));
      writeJsonField(*out, "destination", s.destination, sizeof(s.destination));
      writeJsonField(*out, "via", s.via, sizeof(s.via));
      writeJsonField(*out, "expected", s.etd, sizeof(s.etd));
      writeJsonField(*out, "platform", (showPlatforms && s.serviceType == TRAIN) ? s.platform : "", sizeof(s.platform));
      writeJsonField(*out, "operator", s.opco, sizeof(s.opco));
      out->printf(",\"cancelled\":%s,\"delayed\":%s,\"coaches\":%d,\"bus\":%s", s.isCancelled ? "true" : "false", s.isDelayed ? "true" : "false", s.trainLength, (s.serviceType == BUS) ? "true" : "false");
    } else if (mode == MODE_TUBE) {
      out->print("\"line\":");
      writeJsonString(*out, s.via, sizeof(s.via));
      writeJsonField(*out, "destination", s.destination, sizeof(s.destination));
      out->printf(",\"dueSec\":%d", s.timeToStation);
    } else {
      out->print("\"route\":");
      writeJsonString(*out, s.via, sizeof(s.via));
      writeJsonField(*out, "destination", s.destination, sizeof(s.destination));
      writeJsonField(*out, "time", s.sTime, sizeof(s.sTime));
      writeJsonField(*out, "expected", s.etd, sizeof(s.etd));
    }
    out->print("}");
  }
  out->print("]");

  if (numServices && mode == MODE_RAIL) {
    writeJsonField(*out, "calling", station.calling, sizeof(station.calling));
    writeJsonField(*out, "origin", station.origin, sizeof(station.origin));
    writeJsonField(*out, "serviceMessage", station.serviceMessage, sizeof(station.serviceMessage));
  } else if (numServices && mode == MODE_TUBE) {
    writeJsonField(*out, "currentLocation", station.origin, sizeof(station.origin));
  }

  // Bus data never updates station messages; TfL appends an attribution entry last
  int numMessages = 0;
  if (hasData && mode != MODE_BUS) {
    numMessages = constrain(messages.numMessages, 0, MAXBOARDMESSAGES);
    if (mode == MODE_TUBE && numMessages && strncmp(messages.messages[numMessages - 1], "Powered by TfL", 14) == 0) numMessages--;
  }
  out->print(",\"messages\":[");
  for (int i = 0; i < numMessages; i++) {
    if (i) out->print(",");
    writeJsonString(*out, messages.messages[i], sizeof(messages.messages[i]));
  }
  out->print("]");

  writeJsonField(*out, "weather", weatherEnabled ? weatherMsg : "", sizeof(weatherMsg));
  out->print("}");
  request->send(out);
}

// Send some useful system & station information to the browser
void handleInfo(AsyncWebServerRequest *request) {
  unsigned long uptime = millis();
  char sysUptime[30];
  int days = uptime / msDay ;
  int hours = (uptime % msDay) / msHour;
  int minutes = ((uptime % msDay) % msHour) / msMin;

  sprintf(sysUptime,"%d days, %d hrs, %d min", days,hours,minutes);

  String message = "Free Heap: " + String(ESP.getFreeHeap()) + "\nMin Heap: " + String(ESP.getMinFreeHeap()) + "\nLargest free block: " + String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)) + "\nHostname: " + String(hostname) + "\nFirmware version: v" + String(VERSION_MAJOR) + "." + String(VERSION_MINOR) + " " + getBuildTime() + "\nSystem uptime: " + String(sysUptime) + "\nFree LittleFS space: " + String(LittleFS.totalBytes() - LittleFS.usedBytes());
  message+="\nCore Plaform: " + String(ESP.getCoreVersion()) + "\nCPU speed: " + String(ESP.getCpuFreqMHz()) + "MHz\nCPU Temperature: " + String(temperatureRead()) + "\nWiFi network: " + String(WiFi.SSID()) + "\nWiFi signal strength: " + String(WiFi.RSSI()) + "dB";

  sprintf(sysUptime,"%02d:%02d:%02d %02d/%02d/%04d",timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec,timeinfo.tm_mday,timeinfo.tm_mon+1,timeinfo.tm_year+1900);
  message+="\nSystem clock: " + String(sysUptime);
  if (ghUpdate.releaseId.length()) {
    message+="\nGithub: " + ghUpdate.releaseId;
  }

  if (schedulerActive) message+="\nScheduler active, next event at " + String(nextSlotEventTime);
  else if (carouselActive) message+="\nCarousel active, next event at " + String(nextSlotEventTime);

  message+="\nCurrent location code: " + String(locationCode) + "\nCurrent location name: " + String(locationName) + "\nSuccessful: " + String(dataLoadSuccess) + "\nFailures: " + String(dataLoadFailure) + "\nTime since last data load: " + String((int)((millis()-lastDataLoadTime)/1000)) + " seconds";
  if (dataLoadFailure) message+="\nTime since last failure: " + String((int)((millis()-lastLoadFailure)/1000)) + " seconds";
  message+="\nLast Result: ";
  switch (boardMode) {
    case MODE_RAIL:
      if (useRDMclient) message+="RDMClient: " + String(jsonKeyBuffer.lastResultMessage);
      else message+="darwinClient: " + String(jsonKeyBuffer.lastResultMessage);
      break;

    case MODE_TUBE:
      message+=String(jsonKeyBuffer.lastResultMessage);
      break;

    case MODE_BUS:
      message+=String(jsonKeyBuffer.lastResultMessage);
      break;
  }
  message+="\nUpdate result code: ";
  message+=getResultCodeText(lastUpdateResult);
  message+="\nServices: " + String(station.numServices) + "\nMessages: ";
  int nMsgs = messages.numMessages;
  if (boardMode == MODE_TUBE) nMsgs--;
  message+=String(nMsgs) + "\n";

  if (rssEnabled) {
    message+="Last RSS result: " + getResultCodeText(lastRssUpdateResult) + "\nNext RSS update: " + String(nextRssUpdate-millis()) + "ms\n\n";
  }

  if (weatherEnabled) {
    message+="Last weather result: " + getResultCodeText(lastWeatherUpdateResult) + "\nNext weather update: " + String(nextWeatherUpdate-millis()) + "ms";
  }
  message+="\nScreenshot: " + String(myUrl) + "/screenshot.bmp";
  sendResponse(200,message,request);
}

// Stream the index.htm page unless we're in first time setup and need the api keys
void handleRoot(AsyncWebServerRequest *request) {
  if (!apiKeys) {
    if (LittleFS.exists("/keys.htm")) handleStreamFile("/keys.htm",request); else handleStreamGzipFlashFile("/keys.htm",keyshtm,sizeof(keyshtm),request);
  } else {
    if (LittleFS.exists("/index_d.htm")) handleStreamFile("/index_d.htm",request); else handleStreamGzipFlashFile("/index.htm",indexhtm,sizeof(indexhtm),request);
  }
}

// Send the firmware version to the client (called from index.htm)
void handleFirmwareInfo(AsyncWebServerRequest *request) {
  String response = "{\"firmware\":\"B" + String(VERSION_MAJOR) + "." + String(VERSION_MINOR) + "-W" + String(WEBAPPVER_MAJOR) + "." + String(WEBAPPVER_MINOR) + "\"}";
  request->send(200,contentTypeJson,response);
}

// Force a reboot of the ESP32
void handleReboot(AsyncWebServerRequest *request) {
  sendResponse(200,"The Departures Board is restarting...",request);
  restartTimer.once(1, []() { ESP.restart(); });
}

// Erase the stored WiFiManager credentials
void handleEraseWiFi(AsyncWebServerRequest *request) {
  sendResponse(200,"Erasing stored WiFi settings.\n\nYou will need to connect to the \"Departures Board\" network and use WiFi Manager to reconfigure the settings.",request);
  restartTimer.once(1, []() { WiFiManager wm; wm.resetSettings(); ESP.restart();});
}

// "Factory reset" the app - delete WiFi, format file system and reboot
void handleFactoryReset(AsyncWebServerRequest *request) {
  sendResponse(200,"Factory reseting the Departures Board...",request);
  restartTimer.once(1, []() { WiFiManager wm; wm.resetSettings(); LittleFS.format(); ESP.restart();});
}

// Interactively change the CYD backlight (called from index.htm)
void handleBrightness(AsyncWebServerRequest *request) {
  if (request->hasParam("b")) {
    int level = request->getParam("b")->value().toInt();
    if (level>0 && level<256) {
      u8g2.setContrast(level);
      brightness = level;
      sendResponse(200,"OK",request);
      return;
    }
  }
  sendResponse(200,"invalid request",request);
}

// Interactively change the CYD display appearance (called from index.htm)
#if defined(DISPLAY_CYD)
void handleDisplaySettings(AsyncWebServerRequest *request) {
  if (request->hasParam("color")) {
    int col = request->getParam("color")->value().toInt();
    if (col >= 0 && col <= 5) {
      u8g2.setColorScheme((CydColorScheme)col);
      u8g2.sendBuffer();
    }
  }
  if (request->hasParam("scale")) {
    int scl = request->getParam("scale")->value().toInt();
    if (scl >= 0 && scl <= 1) {
      u8g2.setScaleMode((CydScaleMode)scl);
      u8g2.sendBuffer();
    }
  }
  sendResponse(200,"OK",request);
}
#endif

// Web GUI has requested updates be installed
void handleOtaUpdate(AsyncWebServerRequest *request) {
  sendResponse(200,"Update initiated - check the Departure Board display for progress.",request);
  manualUpdateCheck = true;
}

void doManualOtaCheck() {
  u8g2.clearBuffer();
  u8g2.setFont(NatRailTall12);
  centreText("Getting latest firmware details from GitHub...",26);
  u8g2.sendBuffer();

  if (ghUpdate.getLatestRelease()==UPD_SUCCESS) {
    checkForFirmwareUpdate();
  } else {
    for (int i=15;i>=0;i--) {
      showUpdateCompleteScreen("Firmware Update Check Failed","Unable to retrieve latest release information.",jsonKeyBuffer.lastResultMessage,"",i,false);
      delay(1000);
    }
  }
  // Always restart
  ESP.restart();
}

// Endpoint for controlling sleep mode
void handleControl(AsyncWebServerRequest *request) {
  String resp = "{\"sleeping\":";
  if (request->hasParam("sleep")) {
    if (request->getParam("sleep")->value() == "1") forcedSleep=true; else forcedSleep=false;
  }
  if (request->hasParam("clock")) {
    if (request->getParam("clock")->value() == "1") sleepClock=true; else sleepClock=false;
  }
  resp += (isSleeping || forcedSleep) ? "true":"false";
  resp += ",\"display\":";
  resp += (sleepClock || (!isSleeping && !forcedSleep)) ? "true":"false";
  resp += "}";
  request->send(200, contentTypeJson, resp);
}

/*
 * External data functions - weather, stationpicker, firmware updates
 */

// Call the National Rail Station Picker (called from index.htm)
void handleStationPicker(AsyncWebServerRequest *request)
{
  if (!request->hasParam("q")) {
    sendResponse(400,"Missing Query",request);
    return;
  }

  String query = request->getParam("q")->value();
  if (query.length() <= 2) {
    sendResponse(400,"Query too short",request);
    return;
  }

  const char* host = "stationpicker.nationalrail.co.uk";
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(4000);

  if (!client.connect(host, 443)) {
    sendResponse(408, "NR Connect Timeout",request);
    return;
  }

  client.print(String("GET /stationPicker/") + query + " HTTP/1.0\r\n"
               "Host: stationpicker.nationalrail.co.uk\r\n"
               "Referer: https://www.nationalrail.co.uk\r\n"
               "Origin: https://www.nationalrail.co.uk\r\n"
               "Connection: close\r\n\r\n");

  int requestTimer = 0;
  while (!client.available() && requestTimer<1000) {
    requestTimer++;
    delay(1);
  }

  if (!client.available()) {
    client.stop();
    sendResponse(408,"NRQ Timeout",request);
  }

  String statusLine = client.readStringUntil('\n');

  if (statusLine.indexOf("200") == -1) {
    client.stop();
    sendResponse(503, statusLine, request);
    return;
  }

  // Skip the remaining headers
  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  // Start sending response
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  uint8_t buffer[512];
  unsigned long timeout = millis() + 5000UL;
  while ((client.connected() || client.available()) && millis() < timeout) {
    int len = client.read(buffer, sizeof(buffer));
    if (len > 0) {
      response->write(buffer, len);
      delay(1);
    }
  }

  client.stop();
  request->send(response);
}

/*
 * Setup / Loop functions
*/

//
// The main processing cycle for the National Rail Departures Board
//
void departureBoardLoop() {
#if defined(DISPLAY_CYD)
  u8g2.setFontPosBaseline();
  u8g2.setTextScale(1);
  setRailDetailFont();
#endif

  if (millis() > nextDataUpdate && !fetchInProgress && lastUpdateResult != UPD_UNAUTHORISED && !isSleeping && wifiConnected) {
    if (!firstLoad) showUpdateIcon(true);
    // Initiate a background update on Core 0
    fetchMode = FETCH_BOARD;
    fetchInProgress = true;
    xTaskNotifyGive(fetchTaskHandle);
    if (firstLoad) {
      waitForFirstLoad();
      if (lastUpdateResult == UPD_NO_CHANGE || lastUpdateResult == UPD_SEC_CHANGE) lastUpdateResult = UPD_SUCCESS;
    }
  }

  if (fetchComplete && updateIconVisible) showUpdateIcon(false);

  if (fetchComplete && lastUpdateResult == UPD_SEC_CHANGE && !isScrollingService && !isSleeping) {
    fetchComplete = false;
#if defined(DISPLAY_CYD)
    const int previousPrimaryServiceCount = station.numServices;
    const rdService previousPrimaryService = station.service[0];
    const bool previousPrimaryPlatformAvailable = station.platformAvailable;
    const bool previousPrimaryShowVia = isShowingVia;
#endif
    updateRailDepartures();
    if (station.numServices) {
      if (!station.service[0].via[0]) isShowingVia=false;
#if defined(DISPLAY_CYD)
      if (previousPrimaryServiceCount == 0 || cydPrimaryServiceChanged(previousPrimaryService,previousPrimaryPlatformAvailable)) {
        beginCydPrimaryServiceAnimation(previousPrimaryServiceCount ? &previousPrimaryService : nullptr,
                                        previousPrimaryPlatformAvailable,previousPrimaryShowVia);
      }
      drawCydServicePanel(isShowingVia);
      u8g2.updateDisplayArea(0, CYD_TILE_SERVICE_PANEL_Y, CYD_NATIVE_TILE_WIDTH, CYD_TILE_SERVICE_PANEL_H);
#else
      drawPrimaryService(isShowingVia);
      u8g2.updateDisplayArea(0,1,32,3);
#endif
      if (station.calling[0] && showFullCalling) {
        for (int i=0;i<numMessages;i++) {
          if (strncmp("Calling",line2[i],7)==0) {
            // refresh the calling at times
            sprintf(line2[i],"Calling at: %s",station.calling);
            break;
          }
        }
      }
    }
#if defined(DISPLAY_CYD)
#else
    if (noScrolling && station.numServices>1) {
      drawServiceLine(1,LINE2);
    }
#endif
  }

  if (fetchComplete && lastUpdateResult != UPD_SEC_CHANGE && !isScrollingService && !isSleeping) {
    if (!isScrollingStops || (!showFullCalling && isShowingCalling) || (!showFullMsgs && !isShowingCalling)) {
      fetchComplete = false;
      // Get the update data if there is any
      if (lastUpdateResult == UPD_SUCCESS) {
        // Retrieve the updated data
      #if defined(DISPLAY_CYD)
        const int previousPrimaryServiceCount = station.numServices;
        const rdService previousPrimaryService = station.service[0];
        const bool previousPrimaryPlatformAvailable = station.platformAvailable;
        const bool previousPrimaryShowVia = isShowingVia;
      #endif
        updateRailDepartures();
      #if defined(DISPLAY_CYD)
        if (!firstLoad && station.numServices &&
            (previousPrimaryServiceCount == 0 || cydPrimaryServiceChanged(previousPrimaryService,previousPrimaryPlatformAvailable))) {
          beginCydPrimaryServiceAnimation(previousPrimaryServiceCount ? &previousPrimaryService : nullptr,
                  previousPrimaryPlatformAvailable,previousPrimaryShowVia);
        }
      #endif
        drawStationBoard();
      } else if (lastUpdateResult == UPD_NO_CHANGE) {
        lastDataLoadTime = millis();
        noDataLoaded = false;
        dataLoadSuccess++;
      } else if (lastUpdateResult == UPD_DATA_ERROR || lastUpdateResult == UPD_TIMEOUT || lastUpdateResult == UPD_HTTP_ERROR) {
        lastLoadFailure=millis();
        dataLoadFailure++;
        if (noDataLoaded) showNoDataScreen();
      } else if (lastUpdateResult == UPD_UNAUTHORISED) {
        showTokenErrorScreen();
        while (true) { delay(1);}
      } else {
        dataLoadFailure++;
      }
    }
  }

  if (millis()>timer && numMessages && !isScrollingStops && !isSleeping && lastUpdateResult!=UPD_UNAUTHORISED && lastUpdateResult!=UPD_DATA_ERROR && !noScrolling && !noDataLoaded) {
    // Need to start a new scrolling messages line
    prevMessage = currentMessage;
    prevScrollStopsLength = scrollStopsLength;
    currentMessage++;
    if (currentMessage>=numMessages) currentMessage=0;
    scrollStopsXpos=msgMargin;
#if defined(DISPLAY_CYD)
    scrollStopsYpos=0;
    scrollStopsLength = getStringWidth(line2[currentMessage]);
#else
    scrollStopsYpos=10;
    scrollStopsLength = getStringWidth(line2[currentMessage]);
#endif
    isScrollingStops=true;
    if (strncmp("Calling",line2[currentMessage],7)==0) isShowingCalling=true; else isShowingCalling=false;
  }

  // Check if there's a via destination
  if (millis()>viaTimer) {
    if (station.numServices && station.service[0].via[0] && !isSleeping && lastUpdateResult!=UPD_UNAUTHORISED && lastUpdateResult!=UPD_DATA_ERROR) {
    #if defined(DISPLAY_CYD)
      const rdService previousPrimaryService = station.service[0];
      const bool previousPrimaryShowVia = isShowingVia;
    #endif
      isShowingVia = !isShowingVia;
#if defined(DISPLAY_CYD)
      beginCydPrimaryServiceAnimation(&previousPrimaryService,station.platformAvailable,previousPrimaryShowVia);
      drawCydServicePanel(isShowingVia);
      u8g2.updateDisplayArea(0, CYD_TILE_SERVICE_PANEL_Y, CYD_NATIVE_TILE_WIDTH, CYD_TILE_SERVICE_PANEL_H);
#else
      drawPrimaryService(isShowingVia);
      u8g2.updateDisplayArea(0,1,32,3);
#endif
      if (isShowingVia) viaTimer = millis()+3000; else viaTimer = millis()+4000;
    }
  }

#if defined(DISPLAY_CYD)
  if (!isSleeping && station.numServices) drawPrimaryServiceMessage();

  if (isScrollingCydPrimaryService && !isSleeping && !noServiceClockIsActive && station.numServices) {
    drawCydPrimaryServiceTransition(isShowingVia);
    cydPrimaryServiceScrollY -= 4;
    if (cydPrimaryServiceScrollY <= 0) {
      cydPrimaryServiceScrollY = 0;
      isScrollingCydPrimaryService = false;
      cydPrimaryServiceHasPrevious = false;
      blankArea(0,LINE1,SCREEN_WIDTH,CYD_PRIMARY_SERVICE_SCROLL_DISTANCE);
      drawPrimaryServiceAt(station.service[0],station.platformAvailable,isShowingVia,LINE1);
    }
    u8g2.updateDisplayArea(0,CYD_TILE_PRIMARY_SERVICE_Y,CYD_NATIVE_TILE_WIDTH,CYD_TILE_PRIMARY_SERVICE_H);
  }
#endif

  if (millis()>serviceTimer && !isScrollingService && !isSleeping && !noServiceClockIsActive && !noDataLoaded && lastUpdateResult!=UPD_UNAUTHORISED && lastUpdateResult!=UPD_DATA_ERROR) {
#if defined(DISPLAY_CYD)
    if (station.numServices > 2) {
      prevService = cydSecondaryServiceIndex;
      cydSecondaryServiceIndex++;
      if (cydSecondaryServiceIndex >= station.numServices) {
        cydSecondaryServiceIndex = 1;
      }
      scrollServiceYpos = 44;
      isScrollingService = true;
    } else {
      if (cydSecondaryServiceIndex != 1 && station.numServices == 2) {
        cydSecondaryServiceIndex = 1;
        drawSecondaryServiceLine(1);
        u8g2.updateDisplayArea(0, CYD_TILE_SECONDARY_SERVICE_Y, CYD_NATIVE_TILE_WIDTH, CYD_TILE_SECONDARY_SERVICE_H);
      }
      serviceTimer = millis() + 30000;
    }
#else
    // Need to change to the next service if there is one
    if ((station.numServices <= 1 || (station.numServices==2 && noScrolling)) && !weatherMsg[0]) {
      // There's no other services and no weather so just so static attribution.
      drawServiceLine(1+((station.numServices==2 && noScrolling)?1:0),LINE3);
      serviceTimer = millis() + 30000;
      isScrollingService = false;
    } else {
      prevService = line3Service;
      line3Service++;
      if (station.numServices) {
        if ((line3Service>station.numServices && !weatherMsg[0]) || (line3Service>station.numServices+1 && weatherMsg[0])) line3Service=(noScrolling && station.numServices>1) ? 2:1;  // First 'other' service
      } else {
        if (weatherMsg[0] && line3Service>1) line3Service=0;
      }
      scrollServiceYpos=10;
      isScrollingService = true;
    }
#endif
  }

  if (isScrollingStops && millis()>timer && !isSleeping && !noScrolling) {
#if defined(DISPLAY_CYD)
    blankArea(msgMargin,msgLine,msgWidth,20);
    u8g2.setClipWindow(msgMargin,msgLine,SCREEN_WIDTH,msgLine+20);
    if (scrollStopsLength < msgWidth) {
      centreText(line2[currentMessage],railDetailBaseline(msgLine),msgMargin,msgWidth);
      timer=millis()+6000;
      isScrollingStops=false;
    } else {
      u8g2.drawStr(scrollStopsXpos,railDetailBaseline(msgLine),line2[currentMessage]);
      scrollStopsXpos--;
      if (scrollStopsXpos < -scrollStopsLength+msgMargin) {
        isScrollingStops=false;
        timer=millis()+500;
      }
    }
    u8g2.setMaxClipWindow();
#else
    blankArea(msgMargin,msgLine,msgWidth,9);
    setDisplayClipWindow(msgMargin,msgLine,SCREEN_WIDTH,msgLine+9);
    if (scrollStopsYpos) {
      // we're scrolling up the message initially
      // if the previous message didn't scroll then we need to scroll it up off the screen
      if (prevScrollStopsLength && prevScrollStopsLength<msgWidth) {
        if (strncmp("Calling",line2[prevMessage],7)) centreText(line2[prevMessage],railDetailScrollBaseline(scrollStopsYpos+msgLine-12),msgMargin,msgWidth);
        else u8g2.drawStr(msgMargin,railDetailScrollBaseline(scrollStopsYpos+msgLine-12),line2[prevMessage]); // Handle very short calling at lists
      }
      if (scrollStopsLength<msgWidth && strncmp("Calling",line2[currentMessage],7)) centreText(line2[currentMessage],railDetailScrollBaseline(scrollStopsYpos+msgLine-2),msgMargin,msgWidth); // Centre text if it fits
      else u8g2.drawStr(msgMargin,railDetailScrollBaseline(scrollStopsYpos+msgLine-2),line2[currentMessage]);
      scrollStopsYpos--;
      if (scrollStopsYpos==0) timer=millis()+1500;
    } else {
      // we're scrolling left
      if (scrollStopsLength<msgWidth && strncmp("Calling",line2[currentMessage],7)) centreText(line2[currentMessage],railDetailScrollBaseline(msgLine-1),msgMargin,msgWidth); // Centre text if it fits
      else u8g2.drawStr(scrollStopsXpos,railDetailScrollBaseline(msgLine-1),line2[currentMessage]);
      if (scrollStopsLength < msgWidth) {
        // we don't need to scroll this message, it fits so just set a longer timer
        timer=millis()+6000;
        isScrollingStops=false;
      } else {
        scrollStopsXpos--;
        if (scrollStopsXpos < -scrollStopsLength+msgMargin) {
          isScrollingStops=false;
          timer=millis()+500;  // pause before next message
        }
      }
    }
    u8g2.setMaxClipWindow();
#endif
  }

  if (isScrollingService && !isSleeping && !noServiceClockIsActive) {
#if defined(DISPLAY_CYD)
    blankArea(0,LINE2,SCREEN_WIDTH,LINE3-LINE2);
    if (scrollServiceYpos) {
      u8g2.setClipWindow(0,LINE2,SCREEN_WIDTH,LINE3);
      if (prevService > 0) {
        drawSecondaryServiceLine(prevService, LINE2 - (44 - scrollServiceYpos), false);
      }
      drawSecondaryServiceLine(cydSecondaryServiceIndex, LINE2 + scrollServiceYpos, false);
      u8g2.setMaxClipWindow();
      u8g2.updateDisplayArea(0, CYD_TILE_SECONDARY_SERVICE_Y, CYD_NATIVE_TILE_WIDTH, CYD_TILE_SECONDARY_SERVICE_H);
      scrollServiceYpos -= 4;
      if (scrollServiceYpos <= 0) {
        scrollServiceYpos = 0;
        isScrollingService = false;
        serviceTimer = millis() + 15000;
        drawSecondaryServiceLine(cydSecondaryServiceIndex);
        u8g2.updateDisplayArea(0, CYD_TILE_SECONDARY_SERVICE_Y, CYD_NATIVE_TILE_WIDTH, CYD_TILE_SECONDARY_SERVICE_H);
      }
    }
#else
    if (millis()>serviceTimer) {
      blankArea(0,LINE3,256,9);
      if (scrollServiceYpos) {
        // we're scrolling the service into view
        setDisplayClipWindow(0,LINE3,256,LINE3+9);
        // if the prev service is showing, we need to scroll it up off
        if (prevService>0) drawServiceLine(prevService,scrollServiceYpos+LINE3-12);
        drawServiceLine(line3Service,scrollServiceYpos+LINE3-1);
        u8g2.setMaxClipWindow();
        scrollServiceYpos--;
        if (scrollServiceYpos==0) {
          serviceTimer=millis()+5000;
          isScrollingService=false;
        }
      }
    }
#endif
  }

  if (!isSleeping) {
    // Check if the clock should be updated
    if (!firstLoad) drawCurrentTime();

    // To ensure a consistent refresh rate (for smooth text scrolling), we update the screen every 25ms (around 40fps)
    // so we need to wait any additional ms not used by processing so far before sending the frame to the display controller
    delayMs = frameTimeRail - (millis()-refreshTimer);
    if (delayMs>0) delay(delayMs);
#if defined(DISPLAY_CYD)
    if (!noServiceClockIsActive) {
      if (cydPrimaryMessageCount > 0) {
        u8g2.updateDisplayArea(0, CYD_TILE_PRIMARY_MSG_Y, CYD_NATIVE_TILE_WIDTH, CYD_TILE_PRIMARY_MSG_H);
      }
      if (isScrollingStops) {
        u8g2.updateDisplayArea(0, CYD_TILE_BOTTOM_TICKER_Y, CYD_NATIVE_TILE_WIDTH, CYD_TILE_BOTTOM_TICKER_H);
      }
    } else {
      u8g2.updateDisplayArea(0, CYD_TILE_CLOCK_Y, CYD_NATIVE_TILE_WIDTH, CYD_TILE_CLOCK_H);
    }
#else
    if (!noServiceClockIsActive) u8g2.updateDisplayArea(0,3,32,4); else u8g2.updateDisplayArea(0,6,32,2);
#endif
    refreshTimer=millis();
  }
#if defined(DISPLAY_CYD)
  u8g2.setTextScale(2);
  u8g2.setFont(NatRailSmall9);
#endif
}

//
// Processing loop for London Underground Arrivals board
//
void undergroundArrivalsLoop() {
#if defined(DISPLAY_CYD)
  if (millis()>nextDataUpdate && !fetchInProgress && !isSleeping && wifiConnected) {
    if (!firstLoad) showUpdateIcon(true);
    // Initiate a background update on Core 0
    fetchMode = FETCH_BOARD;
    fetchInProgress = true;
    xTaskNotifyGive(fetchTaskHandle);
    if (firstLoad) waitForFirstLoad();
    if (lastUpdateResult == UPD_NO_CHANGE) lastUpdateResult = UPD_SUCCESS;
  }

  if (fetchComplete && updateIconVisible) showUpdateIcon(false);

  if (fetchComplete && lastUpdateResult == UPD_NO_CHANGE) {
    fetchComplete = false;
    updateArrivals();
    blankArea(0,64,SCREEN_WIDTH,LINE3-64);
    if (station.numServices) {
      drawUndergroundService(0,68,(showTubeCurrentLocation && isShowingVia && station.origin[0]));
      if (station.numServices>1) drawUndergroundService(1,102,false);
      if (station.numServices>2) {
        if (cydSecondaryServiceIndex < 2 || cydSecondaryServiceIndex >= station.numServices) {
          cydSecondaryServiceIndex = 2;
        }
        drawUndergroundService(cydSecondaryServiceIndex,136,false);
      }
    } else {
      centreText("There are no scheduled arrivals at this station.",railDetailBaseline(90));
    }
    u8g2.updateDisplayArea(0, CYD_TILE_SERVICE_PANEL_Y, CYD_NATIVE_TILE_WIDTH, CYD_TILE_SERVICE_PANEL_H);
  }

  if (fetchComplete && lastUpdateResult != UPD_NO_CHANGE && !isScrollingService && !isSleeping) {
    fetchComplete = false;
    if (lastUpdateResult == UPD_SUCCESS) {
      updateArrivals();
      drawUndergroundBoard();
    } else if (lastUpdateResult == UPD_DATA_ERROR || lastUpdateResult == UPD_TIMEOUT || lastUpdateResult == UPD_HTTP_ERROR) {
      lastLoadFailure = millis();
      dataLoadFailure++;
      if (noDataLoaded) showNoDataScreen(); else drawUndergroundBoard();
    } else if (lastUpdateResult == UPD_UNAUTHORISED) {
      showTokenErrorScreen();
      while (true) delay(10);
    } else {
      dataLoadFailure++;
    }
  }

  // Check if we're alternating current location for primary service
  if (showTubeCurrentLocation && millis()>viaTimer && !isSleeping && !noDataLoaded) {
    if (station.numServices && station.origin[0] && lastUpdateResult!=UPD_UNAUTHORISED && lastUpdateResult!=UPD_DATA_ERROR) {
      isShowingVia = !isShowingVia;
      drawUndergroundService(0,68,isShowingVia);
      u8g2.updateDisplayArea(0, 8, CYD_NATIVE_TILE_WIDTH, 5);
      if (isShowingVia) viaTimer = millis()+3000; else viaTimer = millis()+8000;
    }
  }

  // Rotate 3rd service line if there are more than 3 services
  if (station.numServices > 3 && millis() > serviceTimer && !isScrollingService && !isSleeping && !noDataLoaded) {
    prevService = cydSecondaryServiceIndex;
    cydSecondaryServiceIndex++;
    if (cydSecondaryServiceIndex >= station.numServices) cydSecondaryServiceIndex = 2;
    scrollServiceYpos = 22;
    isScrollingService = true;
  }

  if (isScrollingService && !isSleeping) {
    blankArea(0,136,SCREEN_WIDTH,LINE3-136);
    if (scrollServiceYpos) {
      u8g2.setClipWindow(0,136,SCREEN_WIDTH,158);
      drawUndergroundService(prevService, 136 - (22 - scrollServiceYpos), false, false);
      drawUndergroundService(cydSecondaryServiceIndex, 136 + scrollServiceYpos, false, false);
      u8g2.setMaxClipWindow();
      u8g2.updateDisplayArea(0, 16, CYD_NATIVE_TILE_WIDTH, 6);
      scrollServiceYpos -= 2;
      if (scrollServiceYpos <= 0) {
        scrollServiceYpos = 0;
        isScrollingService = false;
        serviceTimer = millis() + 10000;
        drawUndergroundService(cydSecondaryServiceIndex, 136, false);
        u8g2.updateDisplayArea(0, 16, CYD_NATIVE_TILE_WIDTH, 6);
      }
    }
  }

  // Bottom ticker (disruption notices, weather, RSS, attribution)
  if (numMessages > 0 && !isSleeping) {
    if (scrollStopsLength <= SCREEN_WIDTH) {
      if (millis() > timer) {
        currentMessage = (currentMessage + 1) % numMessages;
        scrollStopsLength = getStringWidth(line2[currentMessage]);
        scrollStopsXpos = 0;
        blankArea(0,LINE3,SCREEN_WIDTH,20);
        u8g2.setClipWindow(0,LINE3,SCREEN_WIDTH,LINE3+20);
        if (scrollStopsLength <= SCREEN_WIDTH) {
          centreText(line2[currentMessage],railDetailBaseline(LINE3));
          timer = millis() + 6000;
        } else {
          u8g2.drawStr(scrollStopsXpos,railDetailBaseline(LINE3),line2[currentMessage]);
          timer = millis();
        }
        u8g2.setMaxClipWindow();
        u8g2.updateDisplayArea(0, CYD_TILE_BOTTOM_TICKER_Y, CYD_NATIVE_TILE_WIDTH, CYD_TILE_BOTTOM_TICKER_H);
      }
    } else {
      blankArea(0,LINE3,SCREEN_WIDTH,20);
      u8g2.setClipWindow(0,LINE3,SCREEN_WIDTH,LINE3+20);
      u8g2.drawStr(scrollStopsXpos,railDetailBaseline(LINE3),line2[currentMessage]);
      u8g2.setMaxClipWindow();
      scrollStopsXpos--;
      if (scrollStopsXpos < -scrollStopsLength) {
        currentMessage = (currentMessage + 1) % numMessages;
        scrollStopsLength = getStringWidth(line2[currentMessage]);
        scrollStopsXpos = 0;
        timer = millis() + (scrollStopsLength <= SCREEN_WIDTH ? 6000 : 0);
      }
      u8g2.updateDisplayArea(0, CYD_TILE_BOTTOM_TICKER_Y, CYD_NATIVE_TILE_WIDTH, CYD_TILE_BOTTOM_TICKER_H);
    }
  }

  if (!isSleeping) {
    drawCurrentTime();

    delayMs = frameTimeTube - (millis()-refreshTimer);
    if (delayMs>0) delay(delayMs);
    refreshTimer=millis();
  }
#else
  bool fullRefresh = false;

  if (millis()>nextDataUpdate && !fetchInProgress && !isSleeping && wifiConnected) {
    if (!firstLoad) showUpdateIcon(true);
    // Initiate a background update on Core 0
    fetchMode = FETCH_BOARD;
    fetchInProgress = true;
    xTaskNotifyGive(fetchTaskHandle);
    if (firstLoad) waitForFirstLoad();
    if (lastUpdateResult == UPD_NO_CHANGE) lastUpdateResult = UPD_SUCCESS;
  }

  if (fetchComplete && updateIconVisible) showUpdateIcon(false);

  if (fetchComplete && lastUpdateResult == UPD_NO_CHANGE && !isScrollingPrimary && !isSleeping) {
    fetchComplete = false;
    updateArrivals();
    // Draw the primary service line(s)
    if (station.numServices) {
      drawUndergroundService(0,ULINE1,(showTubeCurrentLocation && isShowingVia && station.origin[0]));
      if (station.numServices>1) drawUndergroundService(1,ULINE2);
    } else {
      u8g2.setFont(Underground10);
      blankArea(0,ULINE1,256,ULINE3-ULINE1);
      centreText("There are no scheduled arrivals at this station.",ULINE1-1);
    }
    fullRefresh = true;
  }

  if (fetchComplete && lastUpdateResult != UPD_NO_CHANGE && (!isScrollingService || !showFullMsgs) && !isScrollingPrimary && !isSleeping) {
    fetchComplete = false;
    isScrollingService = false;
    // Get the updated data
    if (lastUpdateResult == UPD_SUCCESS) {
      updateArrivals();
      drawUndergroundBoard();
    } else if (lastUpdateResult == UPD_DATA_ERROR || lastUpdateResult == UPD_TIMEOUT || lastUpdateResult == UPD_HTTP_ERROR) {
      lastLoadFailure = millis();
      dataLoadFailure++;
      if (noDataLoaded) showNoDataScreen(); else drawUndergroundBoard();
    } else if (lastUpdateResult == UPD_UNAUTHORISED) {
      showTokenErrorScreen();
      while (true) delay(10);
    } else {
      dataLoadFailure++;
    }
  }

  // Check if we're showing currentLocation
  if (showTubeCurrentLocation && millis()>viaTimer) {
    if (station.numServices && station.origin[0] && !isSleeping && lastUpdateResult!=UPD_UNAUTHORISED && lastUpdateResult!=UPD_DATA_ERROR) {
      isShowingVia = !isShowingVia;
      drawUndergroundService(0,ULINE1,isShowingVia);
      u8g2.updateDisplayArea(0,1,32,3);
      if (isShowingVia) viaTimer = millis()+3000; else viaTimer = millis()+8000;
    }
  }

  // Scrolling the additional services
  if (millis()>serviceTimer && !isScrollingService && !isSleeping && !noDataLoaded && lastUpdateResult!=UPD_UNAUTHORISED && lastUpdateResult!=UPD_DATA_ERROR) {
    if (station.numServices<=2 && numMessages==1 && attributionScrolled) {
      // There are no additional services to scroll in so static attribution.
      serviceTimer = millis() + 30000;
    } else {
      // Need to change to the next service or message if there is one
      attributionScrolled = true;
      prevService = line3Service;
      line3Service++;
      scrollServiceYpos=11;
      scrollStopsXpos=0;
      isScrollingService = true;
      if (line3Service>=station.numServices) {
        // Showing the messages
        prevMessage = currentMessage;
        prevScrollStopsLength = scrollStopsLength;  // Save the length of the previous message
        currentMessage++;
        if (currentMessage>=numMessages) {
          if (station.numServices>2) {
            line3Service=2;
            currentMessage=-1; // Rollover back to services
          } else {
            line3Service = station.numServices;
            currentMessage=0;
          }
        }
        scrollStopsLength = getStringWidth(line2[currentMessage]);
      } else {
        scrollStopsLength=SCREEN_WIDTH;
      }
    }
  }

  if (isScrollingService && millis()>serviceTimer && !isSleeping) {
    blankArea(0,ULINE3,256,10);
    if (scrollServiceYpos) {
      // we're scrolling up the message initially
      setDisplayClipWindow(0,ULINE3,256,ULINE3+10);
      // Was the previous display a service?
      if (prevService<station.numServices) {
        drawUndergroundService(prevService,scrollServiceYpos+ULINE3-13);
      } else {
        // if the previous message didn't scroll then we need to scroll it up off the screen
        if (prevScrollStopsLength && prevScrollStopsLength<256) centreText(line2[prevMessage],scrollServiceYpos+ULINE3-13);
      }
      // Is this entry a service?
      if (line3Service<station.numServices) {
        drawUndergroundService(line3Service,scrollServiceYpos+ULINE3-1);
      } else {
        if (scrollStopsLength<256) centreText(line2[currentMessage],scrollServiceYpos+ULINE3-2); // Centre text if it fits
        else u8g2.drawStr(0,scrollServiceYpos+ULINE3-2,line2[currentMessage]);
      }
      u8g2.setMaxClipWindow();
      scrollServiceYpos--;
      if (scrollServiceYpos==0) {
        if (line3Service<station.numServices) {
          serviceTimer=millis()+3500;
          isScrollingService=false;
        } else {
          serviceTimer=millis()+500;
        }
      }
    } else {
      // we're scrolling left
      if (scrollStopsLength<256) centreText(line2[currentMessage],ULINE3-1); // Centre text if it fits
      else u8g2.drawStr(scrollStopsXpos,ULINE3-1,line2[currentMessage]);
      if (scrollStopsLength < 256) {
        // we don't need to scroll this message, it fits so just set a longer timer
        serviceTimer=millis()+3000;
        isScrollingService=false;
      } else {
        scrollStopsXpos--;
        if (scrollStopsXpos < -scrollStopsLength) {
          isScrollingService=false;
          serviceTimer=millis()+500;  // pause before next message
        }
      }
    }
  }

  if (isScrollingPrimary && !isSleeping) {
    blankArea(0,ULINE1,256,ULINE3-ULINE1);
    fullRefresh = true;
    // we're scrolling the primary service(s) into view
    setDisplayClipWindow(0,ULINE1,256,ULINE1+10);
    if (station.numServices) drawUndergroundService(0,scrollPrimaryYpos+ULINE1-1);
    else centreText("There are no scheduled arrivals at this station.",scrollPrimaryYpos+ULINE1-1);
    if (station.numServices>1) {
      setDisplayClipWindow(0,ULINE2,256,ULINE2+10);
      drawUndergroundService(1,scrollPrimaryYpos+ULINE2-1);
    }
    u8g2.setMaxClipWindow();
    scrollPrimaryYpos--;
    if (scrollPrimaryYpos==0) {
      isScrollingPrimary=false;
    }
  }

  if (!isSleeping) {
    // Check if the clock should be updated
    drawCurrentTimeUG();

    delayMs = frameTimeTube - (millis()-refreshTimer);
    if (delayMs>0) delay(delayMs);
    if (fullRefresh) u8g2.updateDisplayArea(0,1,32,6); else u8g2.updateDisplayArea(0,5,32,2);
    refreshTimer=millis();
  }
#endif
}

//
// Processing loop for Bus Departures board
//
void busDeparturesLoop() {
#if defined(DISPLAY_CYD)
  if (millis()>nextDataUpdate && !fetchInProgress && !isSleeping && wifiConnected) {
    if (!firstLoad) showUpdateIcon(true);
    // Initiate a background update on Core 0
    fetchMode = FETCH_BOARD;
    fetchInProgress = true;
    xTaskNotifyGive(fetchTaskHandle);
    if (firstLoad) waitForFirstLoad();
    if (lastUpdateResult == UPD_NO_CHANGE) lastUpdateResult = UPD_SUCCESS;
  }

  if (fetchComplete && updateIconVisible) showUpdateIcon(false);

  if (fetchComplete && lastUpdateResult == UPD_NO_CHANGE) {
    fetchComplete=false;
    updateBusDepartures();
    blankArea(0,64,SCREEN_WIDTH,LINE3-64);
    if (station.numServices) {
      drawBusService(0,68,busDestX);
      if (station.numServices>1) drawBusService(1,102,busDestX);
      if (station.numServices>2) {
        if (cydSecondaryServiceIndex < 2 || cydSecondaryServiceIndex >= station.numServices) {
          cydSecondaryServiceIndex = 2;
        }
        drawBusService(cydSecondaryServiceIndex,136,busDestX);
      }
    } else {
      centreText("There are no scheduled services at this stop.",railDetailBaseline(90));
    }
    u8g2.updateDisplayArea(0, CYD_TILE_SERVICE_PANEL_Y, CYD_NATIVE_TILE_WIDTH, CYD_TILE_SERVICE_PANEL_H);
  }

  if (fetchComplete && lastUpdateResult != UPD_NO_CHANGE && !isScrollingService && !isSleeping) {
    fetchComplete = false;
    if (lastUpdateResult == UPD_SUCCESS) {
      updateBusDepartures();
      drawBusDeparturesBoard();
    } else if (lastUpdateResult == UPD_DATA_ERROR || lastUpdateResult == UPD_TIMEOUT || lastUpdateResult == UPD_HTTP_ERROR) {
      lastLoadFailure = millis();
      dataLoadFailure++;
      if (noDataLoaded) showNoDataScreen(); else drawBusDeparturesBoard();
    } else if (lastUpdateResult == UPD_UNAUTHORISED) {
      showTokenErrorScreen();
      while (true) delay(10);
    } else {
      dataLoadFailure++;
    }
  }

  // Rotate 3rd service line if there are more than 3 services
  if (station.numServices > 3 && millis() > serviceTimer && !isScrollingService && !isSleeping && !noDataLoaded) {
    prevService = cydSecondaryServiceIndex;
    cydSecondaryServiceIndex++;
    if (cydSecondaryServiceIndex >= station.numServices) cydSecondaryServiceIndex = 2;
    scrollServiceYpos = 22;
    isScrollingService = true;
  }

  if (isScrollingService && !isSleeping) {
    blankArea(0,136,SCREEN_WIDTH,LINE3-136);
    if (scrollServiceYpos) {
      u8g2.setClipWindow(0,136,SCREEN_WIDTH,158);
      drawBusService(prevService, 136 - (22 - scrollServiceYpos), busDestX, false);
      drawBusService(cydSecondaryServiceIndex, 136 + scrollServiceYpos, busDestX, false);
      u8g2.setMaxClipWindow();
      u8g2.updateDisplayArea(0, 16, CYD_NATIVE_TILE_WIDTH, 6);
      scrollServiceYpos -= 2;
      if (scrollServiceYpos <= 0) {
        scrollServiceYpos = 0;
        isScrollingService = false;
        serviceTimer = millis() + 10000;
        drawBusService(cydSecondaryServiceIndex, 136, busDestX);
        u8g2.updateDisplayArea(0, 16, CYD_NATIVE_TILE_WIDTH, 6);
      }
    }
  }

  // Bottom ticker (attribution and weather)
  if (messages.numMessages > 0 && !isSleeping) {
    if (scrollStopsLength <= SCREEN_WIDTH) {
      if (millis() > timer) {
        currentMessage = (currentMessage + 1) % messages.numMessages;
        scrollStopsLength = getStringWidth(line2[currentMessage]);
        scrollStopsXpos = 0;
        blankArea(0,LINE3,SCREEN_WIDTH,20);
        u8g2.setClipWindow(0,LINE3,SCREEN_WIDTH,LINE3+20);
        if (scrollStopsLength <= SCREEN_WIDTH) {
          centreText(line2[currentMessage],railDetailBaseline(LINE3));
          timer = millis() + 6000;
        } else {
          u8g2.drawStr(scrollStopsXpos,railDetailBaseline(LINE3),line2[currentMessage]);
          timer = millis();
        }
        u8g2.setMaxClipWindow();
        u8g2.updateDisplayArea(0, CYD_TILE_BOTTOM_TICKER_Y, CYD_NATIVE_TILE_WIDTH, CYD_TILE_BOTTOM_TICKER_H);
      }
    } else {
      blankArea(0,LINE3,SCREEN_WIDTH,20);
      u8g2.setClipWindow(0,LINE3,SCREEN_WIDTH,LINE3+20);
      u8g2.drawStr(scrollStopsXpos,railDetailBaseline(LINE3),line2[currentMessage]);
      u8g2.setMaxClipWindow();
      scrollStopsXpos--;
      if (scrollStopsXpos < -scrollStopsLength) {
        currentMessage = (currentMessage + 1) % messages.numMessages;
        scrollStopsLength = getStringWidth(line2[currentMessage]);
        scrollStopsXpos = 0;
        timer = millis() + (scrollStopsLength <= SCREEN_WIDTH ? 6000 : 0);
      }
      u8g2.updateDisplayArea(0, CYD_TILE_BOTTOM_TICKER_Y, CYD_NATIVE_TILE_WIDTH, CYD_TILE_BOTTOM_TICKER_H);
    }
  }

  if (!isSleeping) {
    drawCurrentTime();

    delayMs = frameTimeBus - (millis()-refreshTimer);
    if (delayMs>0) delay(delayMs);
    refreshTimer=millis();
  }
#else
  bool fullRefresh = false;

  if (millis()>nextDataUpdate && !fetchInProgress && !isSleeping && wifiConnected) {
    if (!firstLoad) showUpdateIcon(true);
    // Initiate a background update on Core 0
    fetchMode = FETCH_BOARD;
    fetchInProgress = true;
    xTaskNotifyGive(fetchTaskHandle);
    if (firstLoad) waitForFirstLoad();
    if (lastUpdateResult == UPD_NO_CHANGE) lastUpdateResult = UPD_SUCCESS;
  }

  if (fetchComplete && updateIconVisible) showUpdateIcon(false);

  if (fetchComplete && lastUpdateResult == UPD_NO_CHANGE) {
    fetchComplete=false;
    updateBusDepartures();
    // Draw the primary service line(s)
    if (station.numServices) {
      drawBusService(0,ULINE1,busDestX);
      if (station.numServices>1) drawBusService(1,ULINE2,busDestX);
    } else {
      u8g2.setFont(NatRailSmall9);
      blankArea(0,ULINE1,256,ULINE3-ULINE1);
      centreText("There are no scheduled services at this stop.",ULINE1-1);
    }
    fullRefresh = true;
  }

  if (fetchComplete && lastUpdateResult != UPD_NO_CHANGE && !isScrollingService && !isScrollingPrimary && !isSleeping) {
    fetchComplete = false;
    if (lastUpdateResult == UPD_SUCCESS) {
      updateBusDepartures();
      drawBusDeparturesBoard();
    } else if (lastUpdateResult == UPD_DATA_ERROR || lastUpdateResult == UPD_TIMEOUT || lastUpdateResult == UPD_HTTP_ERROR) {
      lastLoadFailure = millis();
      dataLoadFailure++;
      if (noDataLoaded) showNoDataScreen(); else drawBusDeparturesBoard();
    } else if (lastUpdateResult == UPD_UNAUTHORISED) {
      showTokenErrorScreen();
      while (true) delay(10);
    } else {
      dataLoadFailure++;
    }
  }

  // Scrolling the additional services
  if (millis()>serviceTimer && !isScrollingPrimary && !isScrollingService && !isSleeping && !noDataLoaded && lastUpdateResult!=UPD_UNAUTHORISED && lastUpdateResult!=UPD_DATA_ERROR) {
    // Need to change to the next service if there is one
    if (station.numServices<=2 && messages.numMessages==1) {
      // There are no additional services or weather to scroll in so static attribution.
      serviceTimer = millis() + 10000;
      line3Service=station.numServices;
    } else {
      // Need to change to the next service or message
      prevService = line3Service;
      line3Service++;
      scrollServiceYpos=11;
      isScrollingService = true;
      if (line3Service>=station.numServices) {
        // Showing the messages
        prevMessage = currentMessage;
        currentMessage++;
        if (currentMessage>=messages.numMessages) {
          if (station.numServices>2) {
            line3Service = 2;
            currentMessage=-1; // Rollover back to services
          } else {
            line3Service = station.numServices;
            currentMessage=0;
          }
        }
      }
    }
  }

  if (isScrollingService && millis()>serviceTimer && !isSleeping) {
    if (scrollServiceYpos) {
      blankArea(0,ULINE3,256,10);
      // we're scrolling up the message
      setDisplayClipWindow(0,ULINE3,256,ULINE3+10);
      // Was the previous display a service?
      if (prevService<station.numServices) {
        drawBusService(prevService,scrollServiceYpos+ULINE3-13,busDestX);
      } else {
        // Scrolling up the previous message
        centreText(line2[prevMessage],scrollServiceYpos+ULINE3-13);
      }
      // Is this entry a service?
      if (line3Service<station.numServices) {
        drawBusService(line3Service,scrollServiceYpos+ULINE3-1,busDestX);
      } else {
        centreText(line2[currentMessage],scrollServiceYpos+ULINE3-2);
      }
      u8g2.setMaxClipWindow();
      scrollServiceYpos--;
      if (scrollServiceYpos==0) {
        serviceTimer = millis()+2800;
        if (station.numServices<=2) serviceTimer+=3000;
      }
    } else isScrollingService=false;
  }

  if (isScrollingPrimary && !isSleeping) {
    blankArea(0,ULINE1,256,ULINE3-ULINE1+10);
    fullRefresh = true;
    // we're scrolling the primary service(s) into view
    setDisplayClipWindow(0,ULINE1,256,ULINE1+10);
    if (station.numServices) drawBusService(0,scrollPrimaryYpos+ULINE1-1,busDestX);
    else centreText("There are no scheduled services at this stop.",scrollPrimaryYpos+ULINE1-1);
    if (station.numServices>1) {
      setDisplayClipWindow(0,ULINE2,256,ULINE2+10);
      drawBusService(1,scrollPrimaryYpos+ULINE2-1,busDestX);
    }
    if (station.numServices>2) {
      setDisplayClipWindow(0,ULINE3,256,ULINE3+10);
      drawBusService(2,scrollPrimaryYpos+ULINE3-1,busDestX);
    } else if (station.numServices<3 && messages.numMessages==1) {
      // scroll up the attribution once...
      setDisplayClipWindow(0,ULINE3,256,ULINE3+10);
      centreText(btAttribution,scrollPrimaryYpos+ULINE3-1);
    }
    u8g2.setMaxClipWindow();
    scrollPrimaryYpos--;
    if (scrollPrimaryYpos==0) {
      isScrollingPrimary=false;
      serviceTimer = millis()+2800;
    }
  }

  if (!isSleeping) {
    // just use the Tube clock for bus mode
    if (drawCurrentTimeUG()) u8g2.setFont(NatRailSmall9);

    delayMs = frameTimeBus - (millis()-refreshTimer);
    if (delayMs>0) delay(delayMs);
    if (fullRefresh) u8g2.updateDisplayArea(0,1,32,6); else u8g2.updateDisplayArea(0,5,32,2);
    refreshTimer=millis();
  }
#endif
}

// The Core 0 Background Task
void fetchDeparturesTask(void *pvParameters) {
  while(true) {
    // Put task to sleep until triggered by Core 1
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Perform the requested data update...
    fetchInProgress = true;
    switch (fetchMode) {
      case FETCH_BOARD:
        switch (boardMode) {
          case MODE_RAIL:
            if (useRDMclient) {
              lastUpdateResult = rdmRailData.fetchDepartures(&station,&messages,locationCode,rdmDeparturesApiKey,rdmServiceApiKey,MAXBOARDSERVICES,enableBus,callingCrsCode,locationCleanFilter,nrTimeOffset,(showLastSeen && !noScrolling),showServiceMsgs);
            } else {
              lastUpdateResult = darwinRailData.fetchDepartures(&station,&messages,locationCode,nrToken,MAXBOARDSERVICES,enableBus,callingCrsCode,locationCleanFilter,nrTimeOffset,(showLastSeen && !noScrolling),showServiceMsgs);
            }
            nextDataUpdate = millis()+apiRefreshRate;
            break;
          case MODE_TUBE:
            lastUpdateResult = tfldata.fetchArrivals(&station,&messages,locationCode,lineId,lineDirection,(noScrolling || !showServiceMsgs),tflAppKey);
            nextDataUpdate = millis() + UGDATAUPDATEINTERVAL; // default update freq
            break;
          case MODE_BUS:
            lastUpdateResult = busdata.fetchDepartures(&station,locationCode,locationCleanFilter);
            nextDataUpdate = millis() + BUSDATAUPDATEINTERVAL;
            break;
        }
        fetchComplete = true;
        break;

      case FETCH_WEATHER:
        // Update the weather forecast
        lastWeatherUpdateResult = currentWeather.updateWeather(openWeatherMapApiKey, locationLat, locationLon);
        nextWeatherUpdate = millis() + WEATHERUPDATEINTERVAL; // update every 20 mins
        weatherFetchComplete = true;
        break;

      case FETCH_RSS:
        // Update the RSS headlines
        lastRssUpdateResult=rss.loadFeed(rssURL);
        nextRssUpdate = millis() + RSSUPDATEINTERVAL;
        rssFetchComplete = true;
        break;
    }

    // Signal to Core 1 that the fetch is complete
    fetchInProgress = false;
  }
}

//
// Setup code
//
void setup(void) {
  // These are the default wsdl XML SOAP entry points. They can be overridden in the config.json file if necessary
  strlcpy(wsdlHost,"lite.realtime.nationalrail.co.uk",sizeof(wsdlHost));
  strlcpy(wsdlAPI,"/OpenLDBWS/wsdl.aspx?ver=2021-11-01",sizeof(wsdlAPI));
  u8g2.begin();
#if defined(DISPLAY_CYD)
  button.begin();
#endif
  u8g2.setContrast(brightness);       // Initial brightness
  u8g2.setDrawColor(1);               // Only a monochrome display, so set the colour to "on"
  u8g2.setFontMode(1);                // Transparent fonts
  u8g2.setFontRefHeightAll();         // Count entire font height
  u8g2.setFontPosTop();               // Reference from top
  u8g2.setFont(NatRailTall12);
  String buildDate = String(__DATE__);
  String notice = "\x80 " + buildDate.substring(buildDate.length()-4) + " Gadec Software (github.com/gadec-uk)";

  bool isFSMounted = LittleFS.begin(true);    // Start the File System, format if necessary
  strcpy(station.location,"");                // No default location
  strcpy(weatherMsg,"");                      // No weather message
  strcpy(nrToken,"");                         // No default National Rail token
  strcpy(tflAppKey,"");                       // No default TfL app_key
  loadApiKeys();                              // Load the API keys from the apiKeys.json
  loadConfig(true);                           // Load the configuration settings from config.json
  u8g2.setContrast(brightness);               // Set the user-saved display brightness
  if (flipScreen) u8g2.setFlipMode(1);
  u8g2.clearBuffer();
#if defined(DISPLAY_CYD)
  u8g2.setTextScale(1);
  u8g2.setFontPosTop();
  const int logoX = (SCREEN_WIDTH - gadeclogo_width) / 2;
  const int logoY = 80;
  u8g2.drawXBM(logoX,logoY,gadeclogo_width,gadeclogo_height,gadeclogo_bits);
  u8g2.setFont(bodyFont());
  centreText(notice.c_str(),logoY + gadeclogo_height + 16);
#else
  u8g2.drawXBM(81,0,gadeclogo_width,gadeclogo_height,gadeclogo_bits);
  centreText(notice.c_str(),48);
#endif
  u8g2.sendBuffer();
  delay(5000);

  u8g2.clearBuffer();
  drawStartupHeading();
  u8g2.sendBuffer();
  progressBar("Connecting to Wi-Fi",20);
  WiFi.mode(WIFI_MODE_NULL);        // Reset the WiFi
  WiFi.setSleep(WIFI_PS_NONE);      // Turn off WiFi Powersaving
  WiFi.hostname(hostname);          // Set the hostname ("Departures Board")
  WiFi.mode(WIFI_STA);              // Enter WiFi station mode

  WiFiManager wm;                             // Start WiFiManager
  wm.setAPCallback(wmConfigModeCallback);     // Set the callback for config mode notification
  wm.setWiFiAutoReconnect(true);              // Attempt to auto-reconnect WiFi
  wm.setConnectTimeout(8);
  wm.setConnectRetries(2);
  std::vector<const char *> menu = {"wifi","exit"};
  wm.setMenu(menu);

  bool result = wm.autoConnect("Departures Board");    // Attempt to connect to WiFi (or enter interactive configuration mode)
  if (!result || wifiConfigured) {
    // Need to restart after config (cannot reuse port)
    ESP.restart();
  }

  // Wait for WiFi connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
  }

  // Get our IP address and store
  updateMyUrl();
  if (MDNS.begin(hostname)) {
    MDNS.addService("http","tcp",80);
  }

  wifiConnected=true;
  WiFi.setAutoReconnect(true);
  u8g2.clearBuffer();                                             // Clear the display
  drawStartupHeading();                                           // Draw the startup heading
  char ipBuff[17];
  WiFi.localIP().toString().toCharArray(ipBuff,sizeof(ipBuff));   // Get the IP address of the ESP32
#if defined(DISPLAY_CYD)
  progressBar("Wi-Fi Connected",30);
  centreText(ipBuff,110);                                         // Display the IP address below progress bar
#else
  centreText(ipBuff,53);                                          // Display the IP address
  progressBar("Wi-Fi Connected",30);
#endif
  u8g2.sendBuffer();                                              // Send to CYD panel

  // Configure authentication middleware
  server.addMiddleware([](AsyncWebServerRequest *request, ArMiddlewareNext next) {
    if (webPassword[0] != '\0') {
      const String &url = request->url();
      bool isPublic = (url == "/live" || url == "/departures" || url == "/departures.json" || url == "/websettings.json" || url == "/screenshot.bmp" || url == "/screenshot" || url == "/info" ||
                       url == "/favicon.png" || url == "/irail.webp" || url == "/itube.webp" ||
                       url == "/ibus.webp" || url == "/nrelogo.webp" || url == "/rdglogo.webp" ||
                       url == "/tfllogo.webp" || url == "/btlogo.webp" || url == "/tube.webp" ||
                       url == "/nr.webp");
      if (!isPublic) {
        if (!request->authenticate(webUsername, webPassword)) {
          return request->requestAuthentication(AsyncAuthType::AUTH_BASIC, "DeparturesBoard", "Authentication required");
        }
      }
    }
    next();
  });

  // Configure the local webserver paths
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){handleRoot(request);});
  server.on("/erasewifi", HTTP_GET, [](AsyncWebServerRequest *request){handleEraseWiFi(request);});
  server.on("/factoryreset", HTTP_GET, [](AsyncWebServerRequest *request){handleFactoryReset(request);});
  server.on("/info", HTTP_GET, [](AsyncWebServerRequest *request){handleInfo(request);});
  server.on("/formatffs", HTTP_GET, [](AsyncWebServerRequest *request){handleFormatFFS(request);});
  server.on("/dir", HTTP_GET, [](AsyncWebServerRequest *request){handleFileList(request);});
  server.onNotFound([](AsyncWebServerRequest *request){handleNotFound(request);});
  server.on("/cat", HTTP_GET, [](AsyncWebServerRequest *request){handleCat(request);});
  server.on("/del", HTTP_GET, [](AsyncWebServerRequest *request){handleDelete(request);});
  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){handleReboot(request);});
  server.on("/stationpicker", HTTP_GET, [](AsyncWebServerRequest *request){handleStationPicker(request);});
  server.on("/firmware", HTTP_GET, [](AsyncWebServerRequest *request){handleFirmwareInfo(request);});
  server.on("/brightness", HTTP_GET, [](AsyncWebServerRequest *request){handleBrightness(request);});
#if defined(DISPLAY_CYD)
  server.on("/display", HTTP_GET, [](AsyncWebServerRequest *request){handleDisplaySettings(request);});
  server.on("/displayinfo", HTTP_GET, [](AsyncWebServerRequest *request){request->send(200,contentTypeJson,"{\"cyd\":true}");});
#else
  server.on("/displayinfo", HTTP_GET, [](AsyncWebServerRequest *request){request->send(200,contentTypeJson,"{\"cyd\":false}");});
#endif
  server.on("/screenshot.bmp", HTTP_GET, [](AsyncWebServerRequest *request){handleScreenshot(request);});
  server.on("/screenshot", HTTP_GET, [](AsyncWebServerRequest *request){handleScreenshot(request);});
  server.on("/live", HTTP_GET, [](AsyncWebServerRequest *request){handleStreamGzipFlashFile("/live.htm",livehtm,sizeof(livehtm),request);});
  server.on("/departures", HTTP_GET, [](AsyncWebServerRequest *request){handleStreamGzipFlashFile("/departures.htm",departureshtm,sizeof(departureshtm),request);});
  server.on("/departures.json", HTTP_GET, [](AsyncWebServerRequest *request){handleDeparturesJson(request);});
  server.on("/websettings.json", HTTP_GET, [](AsyncWebServerRequest *request) {
    File file = LittleFS.open("/config.json", "r");
    if (!file) {
      request->send(500, contentTypeJson, "{\"error\":\"Unable to read board settings\"}");
      return;
    }

    JsonDocument config;
    DeserializationError error = deserializeJson(config, file);
    file.close();
    if (error) {
      request->send(500, contentTypeJson, "{\"error\":\"Unable to parse board settings\"}");
      return;
    }

    JsonDocument webSettings;
    webSettings["webAutoScroll"] = config["webAutoScroll"].is<bool>() ? config["webAutoScroll"].as<bool>() : true;
    int speed = config["webAutoScrollSpeed"].is<int>() ? config["webAutoScrollSpeed"].as<int>() : 60;
    webSettings["webAutoScrollSpeed"] = speed < 10 ? 10 : (speed > 120 ? 120 : speed);

    String response;
    serializeJson(webSettings, response);
    request->send(200, contentTypeJson, response);
  });
  server.on("/ota", HTTP_GET, [](AsyncWebServerRequest *request){handleOtaUpdate(request);});
  server.on("/control", HTTP_GET, [](AsyncWebServerRequest *request){handleControl(request);});
  server.on("/success", HTTP_GET, [](AsyncWebServerRequest *request){request->send(200,contentTypeHtml,successPage);});

  //
  // Save settings returned by the Web GUI
  //
  server.on("/savesettings", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->_tempObject) {
      String* body = (String*)(request->_tempObject);
      saveFile("/config.json", body->c_str());

      JsonDocument doc;
      if (!deserializeJson(doc, body->c_str())) {
        if (doc["webPassword"].is<const char*>()) {
          strlcpy(webPassword, doc["webPassword"], sizeof(webPassword));
        } else {
          strcpy(webPassword, "");
        }
        if (doc["webUsername"].is<const char*>() && strlen(doc["webUsername"]) > 0) {
          strlcpy(webUsername, doc["webUsername"], sizeof(webUsername));
        }
      }

      delete body; // Clean up memory
      request->_tempObject = nullptr;

      if ((!railIsSet && !tubeIsSet && !busIsSet) || (!nrToken[0] && rdmDeparturesApiKey=="" && boardMode==MODE_RAIL) || request->hasParam("reboot")) {
        // First time setup or base config change, we need a full reboot
        sendResponse(200,"Configuration saved. The Departures Board will now restart.",request);
        restartTimer.once(1, []() { ESP.restart(); });
      } else {
        sendResponse(200,"Configuration updated. The Departures Board will update shortly.",request);
        softResetNeeded = true;
      }
    } else {
      sendResponse(400,"Empty",request);
    }
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!index) {
      // First chunk: Create a String object in RAM
      request->_tempObject = new String("");
    }

    String* body = (String*)(request->_tempObject);
    for (size_t i = 0; i < len; i++) {
      body->concat((char)data[i]);
    }
  });

  //
  // Save the API keys returned from the Web GUI
  //
  server.on("/savekeys", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->_tempObject) {
      String* body = (String*)(request->_tempObject);

      JsonDocument doc;
      bool result = true;
      String msg = "The API keys have been saved successfully.";
      DeserializationError error = deserializeJson(doc, body->c_str());
      if (!error) {
        if (!saveFile("/apikeys.json", body->c_str())) {
          msg = "Failed to save the API keys to the file system (file system corrupt or full?)";
          result = false;
        } else {
          JsonObject settings = doc.as<JsonObject>();
          String nrToken = settings["nrToken"].as<String>();
          String rdmDepToken = settings["rdmDepKey"].as<String>();
          if (!nrToken.length() && !rdmDepToken.length()) msg+="\n\nNote: Only Tube and Bus Departures will be available without either Rail Data or National Rail keys.";
        }
      } else {
        msg = "Invalid JSON format. No changes have been saved.";
        result = false;
      }

      delete body; // Clean up memory
      request->_tempObject = nullptr;

      if (result) {
        // Load/Update the API Keys in memory
        loadApiKeys();
        // If all location codes are blank we're in the setup process. If not, the keys have been changed so just reboot.
        if (!railIsSet && !tubeIsSet && !busIsSet) {
          sendResponse(200,msg,request);
          writeDefaultConfig();
          showSetupCrsHelpScreen();
        } else {
          msg += "\n\nThe Departures Board will now restart.";
          sendResponse(200,msg,request);
          restartTimer.once(1, []() { ESP.restart(); });
        }
      } else {
        sendResponse(400,msg,request);
      }
    } else {
      sendResponse(400,"Empty",request);
    }
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!index) {
      // First chunk: Create a String object in RAM
      request->_tempObject = new String("");
    }

    String* body = (String*)(request->_tempObject);
    for (size_t i = 0; i < len; i++) {
      body->concat((char)data[i]);
    }
  });

  //
  // Handle uploads to LittleFS
  //
  server.on("/upload", HTTP_GET, [](AsyncWebServerRequest *request){request->send(200,contentTypeHtml,uploadPage);});
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->redirect("/success");
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
      String path = "/" + filename;
      if (LittleFS.exists(path)) LittleFS.remove(path);
      size_t fileSize = request->header("Content-Length").toInt();
      size_t availableSpace = LittleFS.totalBytes() - LittleFS.usedBytes() - 1024;

      if (fileSize > availableSpace) {
          sendResponse(507,"Insufficient storage space in File System",request);
          request->client()->close();
          return;
      }
      // First chunk: Create/Open the file and store the handle in _tempObject
      // We use a pointer to a File object so we can keep it open between chunks
      File *file = new File(LittleFS.open(path, FILE_WRITE));
      if (!*file) {
        sendResponse(500,"File System Error",request);
        request->client()->close();
        return;
      }
      request->_tempObject = file;
    }

    // If we have a valid file handle, write the current chunk
    if (len && request->_tempObject) {
      File *file = reinterpret_cast<File *>(request->_tempObject);
      file->write(data, len);
    }

    if (final && request->_tempObject) {
      // Last chunk: Close the file and clean up the pointer
      File *file = reinterpret_cast<File *>(request->_tempObject);
      file->close();
      delete file;
      request->_tempObject = nullptr;
    }
  });

  //
  // Handle manual firmware updates at /update
  //
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){request->send(200,contentTypeHtml,updatePage);});
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Check if the Update library encountered any errors.
    bool shouldReboot = !Update.hasError();

    // Create a response. The AJAX script is just looking for a successful HTTP status.
    AsyncWebServerResponse *response = request->beginResponse((shouldReboot ? 200 : 500), "text/plain", (shouldReboot ? "OK" : "FAIL"));
    response->addHeader("Connection", "close");
    request->send(response);

    // If successful, restart the ESP32 to boot into the new firmware
    if (shouldReboot) restartTimer.once(0.5, []() { ESP.restart(); });
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
      // First chunk: Initialize the OTA Update
      // UPDATE_SIZE_UNKNOWN tells the library to just accept chunks until 'final' is true
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        sendResponse(500,"Update begin failed",request);
      }
    }

    // Write chunk data to the flash memory
    if (!Update.hasError() && len) {
      if (Update.write(data, len) != len) {
        sendResponse(500,"Update write failed",request);
      }
    }

    // Final chunk: Close the OTA process
    if (final) {
      if (!Update.end(true)) {
        sendResponse(500,"Update end failed",request);
      }
    }
  });

  server.begin();     // Start the local web server

  // Check for Firmware updates?
  if (firmwareUpdates) {
    progressBar("Checking for firmware updates",40);
    if (ghUpdate.getLatestRelease()==UPD_SUCCESS) {
      checkForFirmwareUpdate();
    } else {
      for (int i=15;i>=0;i--) {
        showUpdateCompleteScreen("Firmware Update Check Failed","Unable to retrieve latest release information.",jsonKeyBuffer.lastResultMessage,"",i,false);
        delay(1000);
      }
      u8g2.clearDisplay();
      drawStartupHeading();
      u8g2.sendBuffer();
    }
  }
  checkPostWebUpgrade();
  // First time configuration?
  if ((!railIsSet && !tubeIsSet && !busIsSet) || (!nrToken[0] && rdmDeparturesApiKey=="" && boardMode==MODE_RAIL)) {
    if (!apiKeys) showSetupKeysHelpScreen();
    else showSetupCrsHelpScreen();
    // First time setup mode will exit with a reboot, so just loop here forever
    while (true) { delay(10); }
  }

  configTzTime(ukTimezone, "uk.pool.ntp.org","time.cloudflare.com","time.windows.com");
  if (timezone!="") {
    setenv("TZ",timezone.c_str(),1);
    tzset();
  }

  // Check the clock has been set successfully before continuing
  int p=50;
  int ntpAttempts=0;
  bool ntpResult=true;
  progressBar("Setting the system clock...",50);
  if(!getLocalTime(&timeinfo,2000)) {              // attempt to set the clock from NTP
    do {
      ntpResult = getLocalTime(&timeinfo,2000);
      ntpAttempts++;
      p+=5;
      progressBar("Setting the system clock...",p);
      if (p>80) p=45;
    } while ((!ntpResult) && (ntpAttempts<10));
  }
  if (!ntpResult) {
    // Sometimes NTP/UDP fails. A reboot usually fixes it.
    progressBar("Failed to set the clock. Rebooting in 5 sec.",0);
    delay(5000);
    ESP.restart();
  }
  prevUpdateCheckDay = timeinfo.tm_mday;
  sprintf(currentTime,"%02d:%02d:%02d",timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec);

  // Reload settings (clock has now been set)
  loadConfig();

  station.numServices=0;
  if (rssEnabled && boardMode!=MODE_BUS) {
    progressBar("Loading RSS headlines feed",60);
    updateRssFeed();
  }

  if (weatherEnabled) {
    progressBar("Getting weather conditions",64);
    updateCurrentWeather(locationLat,locationLon);
  }

  // Create the background task pinned to Core 0
  xTaskCreatePinnedToCore(
    fetchDeparturesTask,  // Task function
    "FetchTask",          // Task name
    10240,                // Stack size (CRITICAL: Needs to be large for SSL/XML)
    NULL,                 // Task parameters
    1,                    // Priority
    &fetchTaskHandle,     // Task handle
    0                     // Core 0 (Network/Background core)
  );

  if (boardMode == MODE_RAIL) {
      if (!useRDMclient) {
        // Using legacy darwin XML client
        progressBar("Initialising National Rail interface",67);
        int res = darwinRailData.init(wsdlHost, wsdlAPI);
        if (res != UPD_SUCCESS) {
          showWsdlFailureScreen();
          while (true) {delay(1);}
        }
      }
      progressBar("Initialising National Rail interface",70);
      rdmRailData.cleanFilter(locationFilter,locationCleanFilter,sizeof(locationFilter));
      startupProgressPercent=70;
  } else if (boardMode == MODE_TUBE) {
      progressBar("Initialising TfL interface",70);
      startupProgressPercent=70;
  } else if (boardMode == MODE_BUS) {
      progressBar("Initialising BusTimes interface",70);
      // Create a cleaned filter
      busdata.cleanFilter(locationFilter,locationCleanFilter,sizeof(locationFilter));
      startupProgressPercent=70;
  }
}


void loop(void) {

  if (touchEnabled) button.updateTouchState();

  if (button.wasShortTapped()) {
    if (isSleeping) {
      if (NSEclockIsActive) NSEclockIsActive = false;
      else forcedAwake = true;
    } else {
      switchToNextMode();
    }
  } else if (button.wasLongTapped() && longPressClock) {
    NSEclockIsActive = !NSEclockIsActive;
    if (NSEclockIsActive) {
      isSleeping = true;
      drawNSEclock(true);
    }
  }

  if (millis()-lastTimeUpdate >= 100) {
    // Update the current time
    int prevSecond = timeinfo.tm_sec;
    if (getLocalTime(&timeinfo)) {
      sprintf(currentTime,"%02d:%02d:%02d",timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec);
      lastTimeUpdate = millis();
      if (millis()>3888000000 && timeinfo.tm_hour==3) ESP.restart(); // Reboot every 45 days at 3am
      if (isSleeping && (useNSEclockForSleep || NSEclockIsActive) && prevSecond != timeinfo.tm_sec) drawNSEclock();  // Update the large clock
    }
  }

  // Check for firmware updates daily if enabled
  if (dailyUpdateCheck && !fetchInProgress && millis()>fwUpdateCheckTimer) {
    fwUpdateCheckTimer = millis() + 3300000 + random(600000); // check again in 55 to 65 mins
    if (timeinfo.tm_mday != prevUpdateCheckDay) {
      if (ghUpdate.getLatestRelease()==UPD_SUCCESS) {
        checkForFirmwareUpdate();
      }
      prevUpdateCheckDay = timeinfo.tm_mday;
    }
  }

  bool wasSleeping = isSleeping;
  isSleeping = isSnoozing();

  if (isSleeping && !useNSEclockForSleep && !NSEclockIsActive && millis()>timer) {       // If the "screensaver" is active, change the screen every 8 seconds
    drawSleepingScreen();
    timer=millis() + SCREENSAVERINTERVAL;
  } else if (wasSleeping && !isSleeping) {
    // Exit sleep mode cleanly
    softResetBoard(MODE_LOADCONFIG);
  } else if (isSleeping && !wasSleeping && useNSEclockForSleep && !NSEclockIsActive) u8g2.setContrast(DIMMED_BRIGHTNESS);

  // WiFi Status icon
  if (WiFi.status() != WL_CONNECTED && wifiConnected) {
    wifiConnected=false;
#if defined(DISPLAY_CYD)
    u8g2.setFont(NatRailTall12);
    u8g2.drawStr(10,224,"\x7F");  // No Wifi Icon
    setRailDetailFont();
    u8g2.updateDisplayArea(0,CYD_TILE_CLOCK_Y,4,CYD_TILE_CLOCK_H);
#else
    u8g2.setFont(NatRailSmall9);
    u8g2.drawStr(0,56,"\x7F");  // No Wifi Icon
    u8g2.updateDisplayArea(0,7,1,1);
#endif
  } else if (WiFi.status() == WL_CONNECTED && !wifiConnected) {
    wifiConnected=true;
#if defined(DISPLAY_CYD)
    blankArea(10,LINE4,16,SCREEN_HEIGHT-LINE4);
    u8g2.updateDisplayArea(0,CYD_TILE_CLOCK_Y,4,CYD_TILE_CLOCK_H);
#else
    blankArea(0,57,5,7);
    u8g2.updateDisplayArea(0,7,1,1);
#endif
    updateMyUrl();  // in case our IP changed
  }

  // Force a manual reset if we've been disconnected for more than 10 secs
  if (WiFi.status() != WL_CONNECTED && millis() > lastWiFiReconnect+10000) {
    WiFi.disconnect();
    delay(100);
    WiFi.reconnect();
    lastWiFiReconnect=millis();
  }

  switch (boardMode) {
    case MODE_RAIL:
      departureBoardLoop();
      break;

    case MODE_TUBE:
      undergroundArrivalsLoop();
      break;

    case MODE_BUS:
      busDeparturesLoop();
      break;
  }

  if (manualUpdateCheck && !fetchInProgress) doManualOtaCheck();

  if (rssEnabled && boardMode != MODE_BUS && millis() > nextRssUpdate && !fetchInProgress && !isSleeping && wifiConnected) {
    // Start an RSS Update on Core 0
    fetchMode = FETCH_RSS;
    fetchInProgress = true;
    xTaskNotifyGive(fetchTaskHandle);
  }

  if (rssFetchComplete) {
    // Background fetch has completed
    rssFetchComplete = false;
    if (lastRssUpdateResult == UPD_SUCCESS) buildRssMessage();
  }

  if (weatherEnabled && millis()>nextWeatherUpdate && !fetchInProgress && locationLat && locationLon && !isSleeping && wifiConnected) {
    // Start a weather update on Core 0
    fetchMode = FETCH_WEATHER;
    fetchInProgress = true;
    xTaskNotifyGive(fetchTaskHandle);
  }

  if (weatherFetchComplete) {
    weatherFetchComplete = false;
    if (lastWeatherUpdateResult == UPD_SUCCESS) {
      strlcpy(weatherMsg,currentWeather.currentWeatherMessage,MAXWEATHERSIZE);
    } else {
      weatherMsg[0] = '\0';
    }
  }

  if (softResetNeeded && !fetchInProgress) {
    softResetNeeded=false;
    softResetBoard(MODE_LOADCONFIG);
  }

  if ((schedulerActive || (carouselActive && numCarouselSlots>1)) && !isSleeping && !fetchInProgress && millis() > nextSchedulerCheck) {
    int nowTime = getTimeInMinutes();
    if ((activeSlotEventTime < nextSlotEventTime && nowTime >= nextSlotEventTime) || (activeSlotEventTime > nextSlotEventTime && nowTime < activeSlotEventTime && nowTime >= nextSlotEventTime)) {
      if (carouselActive) currentCarouselSlot = (currentCarouselSlot + 1) % numCarouselSlots;
      softResetBoard(MODE_LOADCONFIG);
    }
    nextSchedulerCheck = millis() + 10000;  // ten seconds
  }

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.equalsIgnoreCase("snap") || cmd.equalsIgnoreCase("screenshot")) {
      dumpSerialScreenshot();
    } else if (cmd.equalsIgnoreCase("clearpassword") || cmd.equalsIgnoreCase("resetpassword")) {
      strcpy(webPassword, "");
      if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        if (file) {
          JsonDocument doc;
          DeserializationError error = deserializeJson(doc, file);
          file.close();
          if (!error) {
            doc["webPassword"] = "";
            String updated;
            serializeJson(doc, updated);
            saveFile("/config.json", updated.c_str());
          }
        }
      }
      Serial.println("Web interface password cleared.");
    }
  }

}