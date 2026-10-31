// =====================================================================
//  QUNTIS LI-HY-0213-GY — MESURE RADIO 2,4 GHz (ESP32 DevKit V1)
//
//  Cablage : CE=GPIO17  CSN=GPIO5  SCK=GPIO18  MOSI=GPIO23  MISO=GPIO19
//
//  LED bleue de la carte : allumee = appuyer, eteinte = ne pas toucher,
//  clignotement rapide = changement de phase, ne rien compter.
//
//  COMMANDES (terminees par Entree)
//    i                     etat du module
//    a<paires>             balayage alterne silence/appuis, ex: a3
//    l<canal>,<debit>,<n>  ecoute de <n> paires de phases, ex: l85,0,3
//                          debit : 0 = 1 Mbps, 1 = 2 Mbps, 2 = 250 kbps
//    r<canal>,<debit>,<n>  comme l, mais envoie toutes les trames captees
//    s<canal>,<secondes>   ecoute calee sur la synchro XN297, ex: s80,10
//    t<canal>,<n>,<hex>    emet <n> fois les octets bruts apres la synchro
// =====================================================================

#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>

#define CE_PIN 17
#define CSN_PIN 5
#define LED_PIN 2

RF24 radio(CE_PIN, CSN_PIN);

const uint8_t N_CH = 126;
const uint16_t DWELL_MS = 300;
const uint32_t LISTEN_PHASE_MS = 8000;
const uint32_t WARNING_MS = 3000;
const uint8_t PREFIX_LEN = 4;
const uint8_t FRAME_LEN = 32;
const uint8_t MAX_PREFIXES = 96;
const uint8_t MAX_REPORTED = 12;

const rf24_datarate_e RATES[3] = {RF24_1MBPS, RF24_2MBPS, RF24_250KBPS};
const char *RATE_NAME[3] = {"1M", "2M", "250K"};

struct PrefixEntry {
    uint8_t frame[FRAME_LEN];
    uint16_t counts[2];  // 0 = silence, 1 = appuis
};

struct RawFrame {
    uint32_t ms;
    uint8_t pass;
    uint8_t data[FRAME_LEN];
};

const uint16_t MAX_RAW_FRAMES = 2400;
RawFrame *rawFrames = nullptr;

PrefixEntry prefixes[MAX_PREFIXES];
uint8_t prefixCount = 0;
uint16_t perMille[N_CH];

char cmdBuf[96];
uint8_t cmdLen = 0;

// ------------------------------------------------- phases et LED

void warnPhaseChange() {
    unsigned long started = millis();
    while (millis() - started < WARNING_MS) {
        digitalWrite(LED_PIN, (millis() / 100) % 2);
        delay(10);
    }
}

void beginPhase(uint8_t phase) {
    warnPhaseChange();
    digitalWrite(LED_PIN, phase == 1 ? HIGH : LOW);
}

// ------------------------------------------------- balayage d'energie

void radioBase(uint8_t rateIdx) {
    radio.stopListening();
    radio.setAutoAck(false);
    radio.disableCRC();
    radio.setRetries(0, 0);
    radio.setDataRate(RATES[rateIdx]);
}

uint16_t sampleChannel(uint8_t ch) {
    radio.setChannel(ch);
    uint32_t samples = 0, hits = 0;
    unsigned long started = millis();
    while (millis() - started < DWELL_MS) {
        radio.startListening();
        delayMicroseconds(140);
        radio.stopListening();
        if (radio.testRPD()) hits++;
        samples++;
    }
    return samples == 0 ? 0 : (uint16_t)((hits * 1000UL) / samples);
}

void autoScan(uint8_t pairs) {
    radioBase(0);
    radio.setAddressWidth(3);
    radio.setPayloadSize(32);
    Serial.printf("SCAN,start,%u,%u\n", pairs, DWELL_MS);
    for (uint16_t pass = 0; pass < pairs * 2; pass++) {
        uint8_t phase = pass % 2;
        beginPhase(phase);
        for (uint8_t ch = 0; ch < N_CH; ch++) perMille[ch] = sampleChannel(ch);
        Serial.printf("PASS,%u,%c", pass, phase == 1 ? 'P' : 'S');
        for (uint8_t ch = 0; ch < N_CH; ch++) Serial.printf(",%u", perMille[ch]);
        Serial.println();
    }
    digitalWrite(LED_PIN, LOW);
    Serial.println("SCAN,end");
}

// ------------------------------------------------- ecoute universelle

bool isUniformPrefix(const uint8_t *frame) {
    for (uint8_t i = 1; i < PREFIX_LEN; i++) {
        if (frame[i] != frame[0]) return false;
    }
    return frame[0] == 0x00 || frame[0] == 0xFF;
}

void tallyFrame(const uint8_t *frame, uint8_t phase) {
    if (isUniformPrefix(frame)) return;
    for (uint8_t i = 0; i < prefixCount; i++) {
        if (memcmp(prefixes[i].frame, frame, PREFIX_LEN) == 0) {
            prefixes[i].counts[phase]++;
            return;
        }
    }
    // Table pleine : on remplace une entree vue une seule fois.
    uint8_t slot = prefixCount;
    if (prefixCount < MAX_PREFIXES) {
        prefixCount++;
    } else {
        slot = MAX_PREFIXES;
        for (uint8_t i = 0; i < MAX_PREFIXES; i++) {
            if (prefixes[i].counts[0] + prefixes[i].counts[1] <= 1) {
                slot = i;
                break;
            }
        }
        if (slot == MAX_PREFIXES) return;
    }
    memcpy(prefixes[slot].frame, frame, FRAME_LEN);
    prefixes[slot].counts[0] = 0;
    prefixes[slot].counts[1] = 0;
    prefixes[slot].counts[phase] = 1;
}

void reportPrefixes() {
    bool reported[MAX_PREFIXES] = {false};
    for (uint8_t shown = 0; shown < MAX_REPORTED; shown++) {
        int16_t best = -1;
        uint16_t bestCount = 1;
        for (uint8_t i = 0; i < prefixCount; i++) {
            uint16_t total = prefixes[i].counts[0] + prefixes[i].counts[1];
            if (!reported[i] && total > bestCount) {
                bestCount = total;
                best = i;
            }
        }
        if (best < 0) return;
        reported[best] = true;
        Serial.printf("TOP,%u,%u,", prefixes[best].counts[0], prefixes[best].counts[1]);
        for (uint8_t b = 0; b < FRAME_LEN; b++) Serial.printf("%02X", prefixes[best].frame[b]);
        Serial.println();
    }
}

void openPromiscuous(uint8_t ch, uint8_t rateIdx) {
    radioBase(rateIdx);
    radio.setChannel(ch);
    radio.setAddressWidth(2);
    uint8_t pipe0[2] = {0x00, 0xAA};
    uint8_t pipe1[2] = {0x00, 0x55};
    radio.openReadingPipe(0, pipe0);
    radio.openReadingPipe(1, pipe1);
    radio.setPayloadSize(FRAME_LEN);
}

// Garde toutes les trames en memoire, puis les envoie apres la capture
// pour que l'ecriture serie ne fasse pas perdre de paquets.
void capture(uint8_t ch, uint8_t rateIdx, uint8_t pairs) {
    openPromiscuous(ch, rateIdx);
    uint16_t stored = 0;
    uint32_t dropped = 0;
    Serial.printf("CAPTURE,start,%u,%s,%u\n", ch, RATE_NAME[rateIdx], pairs);
    for (uint16_t pass = 0; pass < pairs * 2; pass++) {
        uint8_t phase = pass % 2;
        radio.stopListening();
        beginPhase(phase);
        radio.flush_rx();
        radio.startListening();
        unsigned long started = millis();
        while (millis() - started < LISTEN_PHASE_MS) {
            if (!radio.available()) continue;
            uint8_t frame[FRAME_LEN];
            radio.read(frame, FRAME_LEN);
            if (stored == MAX_RAW_FRAMES) {
                dropped++;
                continue;
            }
            rawFrames[stored].ms = millis();
            rawFrames[stored].pass = pass;
            memcpy(rawFrames[stored].data, frame, FRAME_LEN);
            stored++;
        }
    }
    radio.stopListening();
    digitalWrite(LED_PIN, LOW);
    for (uint16_t i = 0; i < stored; i++) {
        Serial.printf("RAW,%lu,%u,", rawFrames[i].ms, rawFrames[i].pass);
        for (uint8_t b = 0; b < FRAME_LEN; b++) Serial.printf("%02X", rawFrames[i].data[b]);
        Serial.println();
    }
    Serial.printf("CAPTURE,end,%u,%lu\n", stored, dropped);
}

void listen(uint8_t ch, uint8_t rateIdx, uint8_t pairs) {
    openPromiscuous(ch, rateIdx);
    prefixCount = 0;

    uint32_t frames[2] = {0, 0};
    Serial.printf("LISTEN,start,%u,%s,%u\n", ch, RATE_NAME[rateIdx], pairs);
    for (uint16_t pass = 0; pass < pairs * 2; pass++) {
        uint8_t phase = pass % 2;
        radio.stopListening();
        beginPhase(phase);
        radio.flush_rx();
        radio.startListening();
        unsigned long started = millis();
        while (millis() - started < LISTEN_PHASE_MS) {
            if (radio.available()) {
                uint8_t frame[FRAME_LEN];
                radio.read(frame, FRAME_LEN);
                frames[phase]++;
                tallyFrame(frame, phase);
            }
        }
    }
    radio.stopListening();
    digitalWrite(LED_PIN, LOW);
    Serial.printf("FRAMES,%lu,%lu,%u\n", frames[0], frames[1], prefixCount);
    reportPrefixes();
    Serial.println("LISTEN,end");
}

// ------------------------------------------------- commandes

void printInfo() {
    Serial.printf("INFO,connected=%d,plus=%d\n", radio.isChipConnected(), radio.isPVariant());
}

// ------------------------------------------------- protocole XN297

// L'adresse se donne a l'envers : a l'antenne, cela donne 71 0F 55.
const uint8_t XN297_SYNC[3] = {0x55, 0x0F, 0x71};
const uint8_t SNIFF_LEN = 24;
const uint16_t TX_GAP_MS = 4;

void openXn297(uint8_t ch, uint8_t payloadLen) {
    radioBase(0);
    radio.setPALevel(RF24_PA_MAX);
    radio.setChannel(ch);
    radio.setAddressWidth(3);
    radio.setPayloadSize(payloadLen);
}

void sniffSync(uint8_t ch, uint16_t seconds) {
    openXn297(ch, SNIFF_LEN);
    radio.openReadingPipe(1, XN297_SYNC);
    radio.flush_rx();
    radio.startListening();
    digitalWrite(LED_PIN, HIGH);
    Serial.printf("SNIFF,start,%u,%u\n", ch, seconds);
    unsigned long started = millis();
    while (millis() - started < seconds * 1000UL) {
        if (!radio.available()) continue;
        uint8_t frame[SNIFF_LEN];
        radio.read(frame, SNIFF_LEN);
        Serial.printf("PKT,%lu,", millis() - started);
        for (uint8_t b = 0; b < SNIFF_LEN; b++) Serial.printf("%02X", frame[b]);
        Serial.println();
    }
    radio.stopListening();
    digitalWrite(LED_PIN, LOW);
    Serial.println("SNIFF,end");
}

void transmitRaw(uint8_t ch, uint16_t count, const char *hex) {
    size_t hexLen = strlen(hex);
    if (hexLen == 0 || hexLen % 2 != 0 || hexLen / 2 > 32) {
        Serial.println("ERR,hex invalide");
        return;
    }
    uint8_t len = hexLen / 2;
    uint8_t payload[32];
    for (uint8_t i = 0; i < len; i++) {
        char pair[3] = {hex[i * 2], hex[i * 2 + 1], 0};
        payload[i] = (uint8_t)strtol(pair, nullptr, 16);
    }
    openXn297(ch, len);
    radio.openWritingPipe(XN297_SYNC);
    digitalWrite(LED_PIN, HIGH);
    for (uint16_t i = 0; i < count; i++) {
        radio.write(payload, len);
        delay(TX_GAP_MS);
    }
    digitalWrite(LED_PIN, LOW);
    Serial.printf("TX,end,%u,%u\n", ch, count);
}

void handleCommand(const char *cmd) {
    if (cmd[0] == 'i') {
        printInfo();
    } else if (cmd[0] == 's') {
        int ch = 0, seconds = 0;
        if (sscanf(cmd + 1, "%d,%d", &ch, &seconds) != 2 || ch < 0 || ch >= N_CH || seconds < 1 ||
            seconds > 120) {
            Serial.println("ERR,format s<canal>,<secondes>");
            return;
        }
        sniffSync((uint8_t)ch, (uint16_t)seconds);
    } else if (cmd[0] == 't') {
        int ch = 0, count = 0, consumed = 0;
        if (sscanf(cmd + 1, "%d,%d,%n", &ch, &count, &consumed) != 2 || consumed == 0 || ch < 0 ||
            ch >= N_CH || count < 1 || count > 200) {
            Serial.println("ERR,format t<canal>,<n>,<hex>");
            return;
        }
        transmitRaw((uint8_t)ch, (uint16_t)count, cmd + 1 + consumed);
    } else if (cmd[0] == 'a') {
        int pairs = atoi(cmd + 1);
        if (pairs < 1 || pairs > 10) {
            Serial.println("ERR,paires 1-10");
            return;
        }
        autoScan((uint8_t)pairs);
    } else if (cmd[0] == 'l' || cmd[0] == 'r') {
        int ch = 0, rateIdx = 0, pairs = 0;
        if (sscanf(cmd + 1, "%d,%d,%d", &ch, &rateIdx, &pairs) != 3 || ch < 0 || ch >= N_CH ||
            rateIdx < 0 || rateIdx > 2 || pairs < 1 || pairs > 10) {
            Serial.println("ERR,format l<canal>,<debit>,<paires>");
            return;
        }
        if (cmd[0] == 'l') {
            listen((uint8_t)ch, (uint8_t)rateIdx, (uint8_t)pairs);
        } else {
            capture((uint8_t)ch, (uint8_t)rateIdx, (uint8_t)pairs);
        }
    } else {
        Serial.println("ERR,commande inconnue");
    }
}

void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    Serial.begin(115200);
    delay(1200);
    if (!radio.begin()) {
        Serial.println("ERR,NRF24L01 introuvable");
        while (true) delay(1000);
    }
    rawFrames = (RawFrame *)malloc(sizeof(RawFrame) * MAX_RAW_FRAMES);
    if (rawFrames == nullptr) {
        Serial.println("ERR,memoire insuffisante");
        while (true) delay(1000);
    }
    printInfo();
    Serial.println("READY");
}

void loop() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r' || c == '\n') {
            if (cmdLen > 0) {
                cmdBuf[cmdLen] = 0;
                cmdLen = 0;
                handleCommand(cmdBuf);
            }
        } else if (cmdLen < sizeof(cmdBuf) - 1) {
            cmdBuf[cmdLen++] = c;
        }
    }
    delay(5);
}
