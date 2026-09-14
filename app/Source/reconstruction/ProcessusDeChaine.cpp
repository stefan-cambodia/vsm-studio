#include "ProcessusDeChaine.h"
#include <csignal>
#include <cerrno>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <fcntl.h>
#include <chrono>
#include <thread>

namespace vsm::app {

ProcessusDeChaine::~ProcessusDeChaine() {
    if (pid_ > 0 && enVie()) terminer(2000);
    if (tube_ >= 0) ::close(tube_);
}

bool ProcessusDeChaine::demarrer(const juce::StringArray& commande) {
    if (commande.isEmpty() || pid_ != 0) return false;
    std::vector<std::string> mots;
    for (const auto& m : commande) mots.push_back(m.toStdString());
    std::vector<char*> argv;
    for (auto& m : mots) argv.push_back(m.data());
    argv.push_back(nullptr);

    int tube[2];
    if (::pipe(tube) != 0) return false;

    const pid_t enfant = ::fork();
    if (enfant < 0) { ::close(tube[0]); ::close(tube[1]); return false; }
    if (enfant == 0) {
        // DANS L'ENFANT : rien que des appels sûrs après fork. Chef de son
        // groupe, pour qu'un seul `kill(-pgid)` atteigne demucs et les rendus.
        ::setpgid(0, 0);
        ::dup2(tube[1], STDOUT_FILENO);
        ::dup2(tube[1], STDERR_FILENO);
        ::close(tube[0]);
        ::close(tube[1]);
        const int nul = ::open("/dev/null", O_RDONLY);
        if (nul >= 0) { ::dup2(nul, STDIN_FILENO); ::close(nul); }
        ::execvp(argv[0], argv.data());
        ::_exit(127);
    }
    // Le parent pose aussi le groupe : l'un des deux gagne, sans course.
    ::setpgid(enfant, enfant);
    ::close(tube[1]);
    pid_ = enfant;
    tube_ = tube[0];
    return true;
}

int ProcessusDeChaine::lire(char* destination, int taille, int attenteMs) {
    if (tube_ < 0) return -1;
    pollfd p{tube_, POLLIN, 0};
    const int pret = ::poll(&p, 1, attenteMs);
    if (pret <= 0) return 0;
    if (p.revents & POLLIN) {
        const ssize_t lus = ::read(tube_, destination, static_cast<size_t>(taille));
        if (lus > 0) return static_cast<int>(lus);
        if (lus == 0) return -1;                       // fin du flux
        return errno == EINTR || errno == EAGAIN ? 0 : -1;
    }
    if (p.revents & (POLLHUP | POLLERR | POLLNVAL)) return -1;
    return 0;
}

void ProcessusDeChaine::recolter(bool bloquer) {
    if (pid_ <= 0 || recolte_) return;
    int statut = 0;
    const pid_t r = ::waitpid(pid_, &statut, bloquer ? 0 : WNOHANG);
    if (r == pid_) {
        recolte_ = true;
        code_ = WIFEXITED(statut) ? WEXITSTATUS(statut) : -1;
    } else if (r < 0 && errno != EINTR) {
        recolte_ = true;   // déjà récolté ailleurs, ou inexistant
        code_ = -1;
    }
}

bool ProcessusDeChaine::enVie() {
    recolter(false);
    return pid_ > 0 && !recolte_;
}

void ProcessusDeChaine::signalerLeGroupe(int signal) {
    if (pid_ > 0 && !recolte_) ::kill(-pid_, signal);
}

void ProcessusDeChaine::terminer(int delaiMs) {
    if (!enVie()) return;
    signalerLeGroupe(SIGTERM);
    const auto limite = std::chrono::steady_clock::now() + std::chrono::milliseconds(delaiMs);
    while (enVie() && std::chrono::steady_clock::now() < limite)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (enVie()) {
        signalerLeGroupe(SIGKILL);
        recolter(true);
    }
    // Les petits-enfants encore debout après SIGKILL au groupe n'existent pas :
    // le signal atteint chaque membre du groupe, pas seulement le chef.
}

int ProcessusDeChaine::codeDeSortie() {
    recolter(true);
    return code_;
}

} // namespace vsm::app
