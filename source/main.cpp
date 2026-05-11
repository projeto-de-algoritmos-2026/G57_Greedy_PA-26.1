// main.cpp
// SPDX-License-Identifier: BSD-2-Clause

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

// Para suporte de ajuste de console no Windows
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif  // NOMINMAX
#include <Windows.h>
#endif  // _WIN32

#include "IntervalUtils.hpp"
#include "Types.hpp"

// ---------------------------------------------------------------------------
// Utilitários de terminal
// ---------------------------------------------------------------------------

static void clearInput() {
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

static void pausar() {
    std::cout << "\nPressione Enter para continuar...";
    std::cin.get();
}

static void imprimirSeparador() {
    std::cout << "----------------------------------------\n";
}

// Converte "HH:MM" para std::chrono::minutes
static std::chrono::minutes parseTempo(const std::string& s) {
    int h = 0, m = 0;
    char sep;
    std::istringstream ss(s);
    ss >> h >> sep >> m;
    if (ss.fail() || sep != ':' || h < 0 || h >= 24 || m < 0 || m >= 60) {
        throw std::invalid_argument("Formato inválido. Use HH:MM (ex: 08:00)");
    }
    return std::chrono::minutes{h * 60 + m};
}

// Converte std::chrono::minutes para string "HH:MM"
static std::string formatTempo(std::chrono::minutes t) {
    int total = static_cast<int>(t.count());
    char buf[9];
    snprintf(buf, sizeof(buf), "%02d:%02d", total / 60, total % 60);
    return buf;
}

// Lê uma string UTF-8 como u8string
static std::u8string lerU8(const char* prompt) {
    std::cout << prompt;
    std::string s;
    std::getline(std::cin, s);
    return std::u8string(s.begin(), s.end());
}

// Converte u8string para string para exibição
static std::string u8str(const std::u8string& s) {
    return std::string(s.begin(), s.end());
}
static std::string u8str(std::u8string_view s) {
    return std::string(s.begin(), s.end());
}

// Nomes dos dias da semana (domingo = 0)
static const char* nomeDia(unsigned encoding) {
    static const char* nomes[] = {"Domingo", "Segunda", "Terça", "Quarta",
                                  "Quinta",  "Sexta",   "Sábado"};
    return encoding < 7 ? nomes[encoding] : "?";
}

// ---------------------------------------------------------------------------
// Estado global da sessão
// ---------------------------------------------------------------------------

static std::vector<App::Monitoring> g_Monitorias;
static std::vector<App::Class> g_Turmas;
static std::vector<std::u8string> g_Salas;

// ---------------------------------------------------------------------------
// Submenu: gerenciar salas
// ---------------------------------------------------------------------------

static void menuSalas() {
    while (true) {
        std::cout << "\n=== SALAS ===\n";
        if (g_Salas.empty()) {
            std::cout << "  (nenhuma sala cadastrada)\n";
        } else {
            for (size_t i = 0; i < g_Salas.size(); ++i) {
                std::cout << "  [" << i << "] " << u8str(g_Salas[i]) << "\n";
            }
        }
        imprimirSeparador();
        std::cout << "  [1] Adicionar sala\n"
                  << "  [2] Remover sala\n"
                  << "  [0] Voltar\n"
                  << "Opção: ";

        int op = -1;
        std::cin >> op;
        clearInput();

        if (op == 0)
            break;
        if (op == 1) {
            auto nome = lerU8("Nome da sala: ");
            if (!nome.empty()) {
                g_Salas.push_back(nome);
                std::cout << "Sala adicionada.\n";
            }
        } else if (op == 2) {
            if (g_Salas.empty()) {
                std::cout << "Nenhuma sala para remover.\n";
                continue;
            }
            std::cout << "Índice a remover: ";
            int idx;
            std::cin >> idx;
            clearInput();
            if (idx >= 0 && static_cast<size_t>(idx) < g_Salas.size()) {
                g_Salas.erase(g_Salas.begin() + idx);
                std::cout << "Sala removida.\n";
            } else {
                std::cout << "Índice inválido.\n";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Submenu: gerenciar turmas
// ---------------------------------------------------------------------------

static void menuTurmas() {
    while (true) {
        std::cout << "\n=== TURMAS ===\n";
        if (g_Turmas.empty()) {
            std::cout << "  (nenhuma turma cadastrada)\n";
        } else {
            for (size_t i = 0; i < g_Turmas.size(); ++i) {
                const auto& t = g_Turmas[i];
                std::cout << "  [" << i << "] " << t.subject.id << " — Prof. " << u8str(t.teacher)
                          << " — Sala: " << u8str(t.classroom) << "\n";
                for (unsigned d = 0; d < 7; ++d) {
                    if (t.dailyTimes[d].has_value()) {
                        std::cout << "       " << nomeDia(d) << " "
                                  << formatTempo(t.dailyTimes[d]->start) << "-"
                                  << formatTempo(t.dailyTimes[d]->finish) << "\n";
                    }
                }
            }
        }
        imprimirSeparador();
        std::cout << "  [1] Adicionar turma\n"
                  << "  [2] Remover turma\n"
                  << "  [0] Voltar\n"
                  << "Opção: ";

        int op = -1;
        std::cin >> op;
        clearInput();

        if (op == 0)
            break;
        if (op == 1) {
            App::Class turma;
            std::cout << "ID da disciplina (ex: CIC0110): ";
            std::getline(std::cin, turma.subject.id);
            turma.subject.name = lerU8("Nome da disciplina: ");
            turma.teacher = lerU8("Professor: ");
            turma.classroom = lerU8("Sala: ");

            std::cout << "Dias com aula (ex: 2 4 para Seg e Qua, 0=Dom..6=Sab): ";
            std::string linha;
            std::getline(std::cin, linha);
            std::istringstream ss(linha);
            int d;
            while (ss >> d) {
                if (d < 0 || d > 6)
                    continue;
                try {
                    std::cout << "  Início " << nomeDia(d) << " (HH:MM): ";
                    std::string ts;
                    std::getline(std::cin, ts);
                    auto inicio = parseTempo(ts);
                    std::cout << "  Fim    " << nomeDia(d) << " (HH:MM): ";
                    std::getline(std::cin, ts);
                    auto fim = parseTempo(ts);
                    turma.dailyTimes[d] = App::Class::Times{inicio, fim};
                } catch (const std::exception& e) {
                    std::cout << "  Erro: " << e.what() << " — dia ignorado.\n";
                }
            }
            g_Turmas.push_back(std::move(turma));
            std::cout << "Turma adicionada.\n";
        } else if (op == 2) {
            if (g_Turmas.empty()) {
                std::cout << "Nenhuma turma.\n";
                continue;
            }
            std::cout << "Índice a remover: ";
            int idx;
            std::cin >> idx;
            clearInput();
            if (idx >= 0 && static_cast<size_t>(idx) < g_Turmas.size()) {
                g_Turmas.erase(g_Turmas.begin() + idx);
                std::cout << "Turma removida.\n";
            } else {
                std::cout << "Índice inválido.\n";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Submenu: gerenciar monitorias
// ---------------------------------------------------------------------------

static void menuMonitorias() {
    while (true) {
        std::cout << "\n=== MONITORIAS ===\n";
        if (g_Monitorias.empty()) {
            std::cout << "  (nenhuma monitoria cadastrada)\n";
        } else {
            for (size_t i = 0; i < g_Monitorias.size(); ++i) {
                const auto& m = g_Monitorias[i];
                std::cout << "  [" << i << "] " << m.subject.id
                          << " — Monitor: " << u8str(m.monitor) << " — "
                          << nomeDia(m.wd.c_encoding()) << " " << formatTempo(m.times.start) << "-"
                          << formatTempo(m.times.finish) << "\n";
            }
        }
        imprimirSeparador();
        std::cout << "  [1] Adicionar monitoria\n"
                  << "  [2] Remover monitoria\n"
                  << "  [0] Voltar\n"
                  << "Opção: ";

        int op = -1;
        std::cin >> op;
        clearInput();

        if (op == 0)
            break;
        if (op == 1) {
            App::Monitoring mon;
            std::cout << "ID da disciplina: ";
            std::getline(std::cin, mon.subject.id);
            mon.subject.name = lerU8("Nome da disciplina: ");
            mon.monitor = lerU8("Monitor: ");

            std::cout << "Dia da semana (0=Dom, 1=Seg, ..., 6=Sab): ";
            int d;
            std::cin >> d;
            clearInput();
            if (d < 0 || d > 6) {
                std::cout << "Dia inválido.\n";
                continue;
            }
            mon.wd = std::chrono::weekday{static_cast<unsigned>(d)};

            try {
                std::cout << "Início (HH:MM): ";
                std::string ts;
                std::getline(std::cin, ts);
                auto inicio = parseTempo(ts);
                std::cout << "Fim    (HH:MM): ";
                std::getline(std::cin, ts);
                auto fim = parseTempo(ts);
                mon.times = {inicio, fim};
            } catch (const std::exception& e) {
                std::cout << "Erro: " << e.what() << "\n";
                continue;
            }
            g_Monitorias.push_back(std::move(mon));
            std::cout << "Monitoria adicionada.\n";
        } else if (op == 2) {
            if (g_Monitorias.empty()) {
                std::cout << "Nenhuma monitoria.\n";
                continue;
            }
            std::cout << "Índice a remover: ";
            int idx;
            std::cin >> idx;
            clearInput();
            if (idx >= 0 && static_cast<size_t>(idx) < g_Monitorias.size()) {
                g_Monitorias.erase(g_Monitorias.begin() + idx);
                std::cout << "Monitoria removida.\n";
            } else {
                std::cout << "Índice inválido.\n";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Algoritmo 1: Interval Scheduling — máximo de monitorias sem conflito
// ---------------------------------------------------------------------------

static void executarScheduling() {
    std::cout << "\n=== INTERVAL SCHEDULING — Máximo de monitorias ===\n";
    if (g_Monitorias.empty()) {
        std::cout << "Nenhuma monitoria cadastrada.\n";
        pausar();
        return;
    }

    try {
        auto resultado = App::tryScheduleMonitorings(g_Monitorias, g_Turmas);

        std::cout << "\nMonitorias selecionadas (" << resultado.size() << "):\n";
        imprimirSeparador();
        for (const App::Monitoring* mon : resultado) {
            std::cout << "  " << mon->subject.id << " | Monitor: " << u8str(mon->monitor) << " | "
                      << nomeDia(mon->wd.c_encoding()) << " " << formatTempo(mon->times.start)
                      << " - " << formatTempo(mon->times.finish) << "\n";
        }
        imprimirSeparador();
        std::cout << "Total: " << resultado.size() << " de " << g_Monitorias.size()
                  << " monitorias agendadas.\n";
    } catch (const std::exception& e) {
        std::cout << "Erro: " << e.what() << "\n";
    }
    pausar();
}

// ---------------------------------------------------------------------------
// Algoritmo 2: Interval Partitioning — distribuir monitorias nas salas
// ---------------------------------------------------------------------------

static void executarPartitioning() {
    std::cout << "\n=== INTERVAL PARTITIONING — Distribuição de salas ===\n";

    if (g_Monitorias.empty()) {
        std::cout << "Nenhuma monitoria cadastrada.\n";
        pausar();
        return;
    }
    if (g_Salas.empty()) {
        std::cout << "Nenhuma sala cadastrada.\n";
        pausar();
        return;
    }

    std::vector<std::u8string_view> assignedClassrooms(g_Monitorias.size());

    try {
        bool ok = App::tryPartitionMonitorings(g_Monitorias, g_Salas, assignedClassrooms.data(),
                                               g_Turmas);

        if (!ok) {
            std::cout
                << "\n⚠ Não foi possível alocar todas as monitorias com as salas disponíveis.\n";
            pausar();
            return;
        }

        std::cout << "\nAlocação de salas:\n";
        imprimirSeparador();
        for (size_t i = 0; i < g_Monitorias.size(); ++i) {
            const auto& mon = g_Monitorias[i];
            std::cout << "  " << mon.subject.id << " | " << nomeDia(mon.wd.c_encoding()) << " "
                      << formatTempo(mon.times.start) << "-" << formatTempo(mon.times.finish)
                      << " → Sala: " << u8str(assignedClassrooms[i]) << "\n";
        }
        imprimirSeparador();
        std::cout << "Todas as " << g_Monitorias.size()
                  << " monitorias foram alocadas com sucesso.\n";
    } catch (const std::exception& e) {
        std::cout << "Erro: " << e.what() << "\n";
    }
    pausar();
}

// ---------------------------------------------------------------------------
// Dados de exemplo para demonstração rápida
// ---------------------------------------------------------------------------

static void carregarExemplo() {
    g_Salas.clear();
    g_Turmas.clear();
    g_Monitorias.clear();

    // Salas
    g_Salas.push_back(u8"I10 - Lab 1");
    g_Salas.push_back(u8"I10 - Lab 2");
    g_Salas.push_back(u8"S10 - Sala 1");

    // Turma de Algoritmos — Segunda e Quarta 08:00-10:00 no Lab 1
    App::Class turmaAlg;
    turmaAlg.subject = {"CIC0110", u8"Algoritmos e Estruturas de Dados"};
    turmaAlg.teacher = u8"Prof. Silva";
    turmaAlg.classroom = u8"I10 - Lab 1";
    turmaAlg.dailyTimes[1] =
        App::Class::Times{std::chrono::minutes{8 * 60}, std::chrono::minutes{10 * 60}};
    turmaAlg.dailyTimes[3] =
        App::Class::Times{std::chrono::minutes{8 * 60}, std::chrono::minutes{10 * 60}};
    g_Turmas.push_back(turmaAlg);

    // Monitorias
    auto addMon = [&](const char* id, const char* monitor, unsigned day, int hIni, int mIni,
                      int hFim, int mFim) {
        App::Monitoring mon;
        mon.subject = {id, std::u8string(reinterpret_cast<const char8_t*>(id))};
        mon.monitor = std::u8string(reinterpret_cast<const char8_t*>(monitor));
        mon.wd = std::chrono::weekday{day};
        mon.times = {std::chrono::minutes{hIni * 60 + mIni},
                     std::chrono::minutes{hFim * 60 + mFim}};
        g_Monitorias.push_back(mon);
    };

    addMon("CIC0110", "Ana", 1, 10, 0, 12, 0);    // Seg 10h-12h
    addMon("CIC0110", "Bruno", 1, 11, 0, 13, 0);  // Seg 11h-13h (conflito c/ Ana)
    addMon("CIC0110", "Carla", 1, 14, 0, 16, 0);  // Seg 14h-16h
    addMon("CIC0110", "Diego", 3, 10, 0, 12, 0);  // Qua 10h-12h
    addMon("CIC0110", "Elena", 3, 13, 0, 15, 0);  // Qua 13h-15h
    addMon("CIC0110", "Felipe", 5, 9, 0, 11, 0);  // Sex 09h-11h

    std::cout << "Exemplo carregado: 3 salas, 1 turma, 6 monitorias.\n";
    pausar();
}

// ---------------------------------------------------------------------------
// Menu principal
// ---------------------------------------------------------------------------

int main() {
    // Setar locale para UTF-8
#ifdef _WIN32
    // Setar console do Windows para a página de código UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Habilitar processamento de terminal virtual para saída UTF-8 apropriada
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            mode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, mode);
        }
    }
#endif

    std::cin.imbue(std::locale("pt_BR.UTF-8"));
    std::cout.imbue(std::locale("pt_BR.UTF-8"));
    std::cerr.imbue(std::locale("pt_BR.UTF-8"));

    while (true) {
        std::cout << "\n╔════════════════════════════════════════╗\n"
                  << "║   Agendamento de Monitorias — G57      ║\n"
                  << "╠════════════════════════════════════════╣\n"
                  << "║  Dados                                 ║\n"
                  << "║  [1] Gerenciar salas (" << g_Salas.size() << ")";
        for (int i = (int)std::to_string(g_Salas.size()).size(); i < 16; ++i)
            std::cout << ' ';
        std::cout << "║\n"
                  << "║  [2] Gerenciar turmas (" << g_Turmas.size() << ")";
        for (int i = (int)std::to_string(g_Turmas.size()).size(); i < 15; ++i)
            std::cout << ' ';
        std::cout << "║\n"
                  << "║  [3] Gerenciar monitorias (" << g_Monitorias.size() << ")";
        for (int i = (int)std::to_string(g_Monitorias.size()).size(); i < 11; ++i)
            std::cout << ' ';
        std::cout << "║\n"
                  << "╠════════════════════════════════════════╣\n"
                  << "║  Algoritmos                            ║\n"
                  << "║  [4] Interval Scheduling (max monit.)  ║\n"
                  << "║  [5] Interval Partitioning (salas)     ║\n"
                  << "╠════════════════════════════════════════╣\n"
                  << "║  [6] Carregar exemplo                  ║\n"
                  << "║  [0] Sair                              ║\n"
                  << "╚════════════════════════════════════════╝\n"
                  << "Opção: ";

        int op = -1;
        std::cin >> op;
        clearInput();

        switch (op) {
        case 1:
            menuSalas();
            break;
        case 2:
            menuTurmas();
            break;
        case 3:
            menuMonitorias();
            break;
        case 4:
            executarScheduling();
            break;
        case 5:
            executarPartitioning();
            break;
        case 6:
            carregarExemplo();
            break;
        case 0:
            std::cout << "Saindo...\n";
            goto Exit;
        default:
            std::cout << "Opção inválida.\n";
        }
    }

Exit:

    return EXIT_SUCCESS;
}
