// Genera el juego de sonidos inicial del proyecto (WAV PCM 16 bits, 44,1 kHz, mono), sintetizados.
// Uso: ge3d_sound_gen <carpeta Content> [--force]
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr int rate = 44100;
constexpr double pi = 3.14159265358979;

struct Random {
    std::uint32_t state = 0x12345678u;
    double next() {   // -1..1
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double>(state) / 2147483648.0 - 1.0;
    }
};

using Samples = std::vector<double>;

Samples silence(double seconds) {
    return Samples(static_cast<std::size_t>(seconds * rate), 0.0);
}

double note(int semitone) {   // 0 = La 440
    return 440.0 * std::pow(2.0, semitone / 12.0);
}

// Campanita de caja de música: seno con un armónico, ataque corto y caída exponencial.
void addChime(Samples& out, double start, double frequency, double gain, double decay) {
    const auto first = static_cast<std::size_t>(start * rate);
    for (std::size_t i = first; i < out.size(); ++i) {
        const double t = static_cast<double>(i - first) / rate;
        const double envelope = std::min(1.0, t * 400.0) * std::exp(-t * decay);
        if (envelope < 1e-4 && t > 0.1) break;
        out[i] += gain * envelope * (std::sin(2 * pi * frequency * t) + 0.3 * std::sin(2 * pi * frequency * 4.0 * t) * std::exp(-t * 8));
    }
}

void normalize(Samples& samples, double peak) {
    double maximum = 1e-9;
    for (const double s : samples) maximum = std::max(maximum, std::abs(s));
    for (double& s : samples) s *= peak / maximum;
}

// Bucle sin salto: los últimos milisegundos se funden con los primeros.
void makeLoop(Samples& samples, double seconds) {
    const auto fade = static_cast<std::size_t>(seconds * rate);
    const std::size_t n = samples.size();
    for (std::size_t i = 0; i < fade; ++i) {
        const double a = static_cast<double>(i) / static_cast<double>(fade);
        samples[i] = samples[i] * a + samples[n - fade + i] * (1 - a);
    }
    samples.resize(n - fade);
}

bool writeWav(const std::filesystem::path& path, const Samples& samples) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    const auto put32 = [&](std::uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
    const auto put16 = [&](std::uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<std::uint32_t>(samples.size() * 2);
    file.write("RIFF", 4);
    put32(36 + bytes);
    file.write("WAVEfmt ", 8);
    put32(16);
    put16(1);          // PCM
    put16(1);          // mono
    put32(rate);
    put32(rate * 2);
    put16(2);
    put16(16);
    file.write("data", 4);
    put32(bytes);
    for (const double s : samples) {
        const auto v = static_cast<std::int16_t>(std::lround(std::clamp(s, -1.0, 1.0) * 32767.0));
        file.write(reinterpret_cast<const char*>(&v), 2);
    }
    return static_cast<bool>(file);
}

Samples musicBox() {
    // Arpegio de 4 compases (Do - La m - Fa - Sol) a 100 pulsos por minuto.
    const double beat = 60.0 / 100.0 / 2.0;
    Samples out = silence(beat * 32 + 1.5);
    const int chords[4][4] = {{-9, -5, -2, 3}, {-12, -9, -5, 0}, {-16, -12, -9, -4}, {-14, -10, -7, -2}};
    for (int bar = 0; bar < 4; ++bar) {
        for (int step = 0; step < 8; ++step) {
            const int pattern[8] = {0, 1, 2, 3, 2, 1, 2, 3};
            addChime(out, (bar * 8 + step) * beat, note(chords[bar][pattern[step]] + 12), step == 0 ? 0.9 : 0.6, 3.5);
        }
        addChime(out, bar * 8 * beat, note(chords[bar][0]), 0.5, 1.5);
    }
    // La cola de las últimas notas se suma al principio: el bucle no corta.
    const auto loopLength = static_cast<std::size_t>(beat * 32 * rate);
    for (std::size_t i = loopLength; i < out.size(); ++i) out[i - loopLength] += out[i];
    out.resize(loopLength);
    normalize(out, 0.7);
    return out;
}

Samples fountain() {
    // Agua: ruido filtrado con burbujas (senos cortos de tono que sube).
    Random random;
    Samples out = silence(4.25);
    double low = 0, band = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double white = random.next();
        low += 0.08 * (white - low);
        band += 0.3 * ((white - low) - band);
        out[i] = band * 0.5 + low * 0.3;
    }
    for (int bubble = 0; bubble < 70; ++bubble) {
        const double start = (random.next() * 0.5 + 0.5) * 4.0;
        const double frequency = 500 + (random.next() * 0.5 + 0.5) * 900;
        const auto first = static_cast<std::size_t>(start * rate);
        for (std::size_t i = first; i < out.size() && i < first + rate / 12; ++i) {
            const double t = static_cast<double>(i - first) / rate;
            out[i] += 0.25 * std::exp(-t * 60) * std::sin(2 * pi * frequency * (1 + t * 6) * t);
        }
    }
    makeLoop(out, 0.25);
    normalize(out, 0.6);
    return out;
}

Samples creak() {
    Random random;
    Samples out = silence(0.7);
    double phase = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        const double frequency = 90 + 60 * std::sin(t * 9) + 40 * t;
        phase += frequency / rate;
        // Pulsos irregulares de fricción (la madera "raspa").
        const double pulse = std::pow(std::max(0.0, std::sin(2 * pi * phase)), 12.0);
        const double envelope = std::min(1.0, t * 20) * std::exp(-t * 2.5);
        out[i] = envelope * (pulse * 0.8 + random.next() * 0.08);
    }
    normalize(out, 0.7);
    return out;
}

Samples thump() {
    Random random;
    Samples out = silence(0.35);
    double low = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        low += 0.05 * (random.next() - low);
        out[i] = std::exp(-t * 18) * std::sin(2 * pi * (70 + 60 * std::exp(-t * 30)) * t) + std::exp(-t * 40) * low * 2;
    }
    normalize(out, 0.8);
    return out;
}

Samples step() {
    Random random;
    Samples out = silence(0.14);
    double low = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        low += 0.15 * (random.next() - low);
        out[i] = std::exp(-t * 45) * low;
    }
    normalize(out, 0.5);
    return out;
}

Samples jump() {
    Samples out = silence(0.18);
    double phase = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        phase += (300 + 900 * t / 0.18) / rate;
        out[i] = (std::fmod(phase, 1.0) < 0.5 ? 0.4 : -0.4) * std::exp(-t * 12);   // onda cuadrada de 8 bits
    }
    normalize(out, 0.45);
    return out;
}

Samples bell() {
    Samples out = silence(1.6);
    addChime(out, 0, note(3), 1.0, 2.5);
    addChime(out, 0, note(10), 0.5, 3.0);
    addChime(out, 0.12, note(15), 0.6, 3.0);
    normalize(out, 0.7);
    return out;
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "uso: ge3d_sound_gen <carpeta Content> [--force]\n";
        return 2;
    }
    const bool force = argc > 2 && std::string(argv[2]) == "--force";
    const auto folder = std::filesystem::path(argv[1]) / "sounds";
    std::filesystem::create_directories(folder);
    const std::pair<const char*, std::function<Samples()>> sounds[] = {
        {"musica_caja.wav", musicBox}, {"fuente.wav", fountain}, {"puerta.wav", creak}, {"golpe.wav", thump},
        {"paso.wav", step},           {"salto.wav", jump},       {"campana.wav", bell},
    };
    int written = 0;
    for (const auto& [name, make] : sounds) {
        const auto path = folder / name;
        if (std::filesystem::exists(path) && !force) continue;
        if (!writeWav(path, make())) {
            std::cerr << "no se pudo escribir " << path.string() << '\n';
            return 1;
        }
        ++written;
    }
    std::cout << written << " sonidos escritos en " << folder.string() << '\n';
    return 0;
}
