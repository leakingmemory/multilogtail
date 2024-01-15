#include <iostream>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <ncurses.h>
#include <unistd.h>

template <typename TP>
std::time_t to_time_t(TP tp)
{
    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(tp - TP::clock::now()
                                                        + system_clock::now());
    return system_clock::to_time_t(sctp);
}

std::vector<std::string> FindLatestFiles(const std::string &path, int num) {
    std::vector<std::tuple<std::filesystem::file_time_type,std::string>> withTm{};
    for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(path)) {
        if (!entry.is_regular_file())
            continue;
        std::string p = entry.path();
        std::filesystem::file_time_type tm = entry.last_write_time();
        withTm.emplace_back(tm,p);
    }
    std::sort(withTm.begin(), withTm.end(), [] (auto a, auto b) {return std::get<0>(a) >= std::get<0>(b);});
    std::vector<std::string> latest{};
    for (const auto &l : withTm) {
        //std::cout << to_time_t(std::get<0>(l)) << "\n";
        latest.emplace_back(std::get<1>(l));
        if (latest.size() >= num) {
            return latest;
        }
    }
    return latest;
}

int main(int argc, char *argv[]) {
    std::string path{"."};
    if (argc > 1) {
        path = argv[1];
    }
    int num = 2;
    std::vector<std::string> watching = FindLatestFiles(path, num);
    std::vector<std::string> pending{};
    std::vector<std::fstream> tails{};
    std::vector<int> pos{};
    std::vector<uint64_t> iterationCount{};
    for (const auto &path : watching) {
        auto &fstream = tails.emplace_back();
        std::cout << path << "\n";
        fstream.open(path, std::ios::in);
        pos.emplace_back(0);
        iterationCount.emplace_back(0);
    }
    sleep(4);
    initscr();
    cbreak();
    noecho();
    uint64_t iterationNum = 0;
    uint64_t pendingIteration = 0;
    bool inputPrevious{false};
    while (true) {
        if (!inputPrevious) {
            sleep(1);
        }
        inputPrevious = false;
        ++iterationNum;
        {
            std::vector<std::string> latest = FindLatestFiles(path, num);
            std::vector<std::string> newPending{};
            for (const auto &l : latest) {
                bool found{false};
                for (const auto &w : watching) {
                    if (w == l) {
                        found = true;
                    }
                }
                if (!found) {
                    newPending.emplace_back(l);
                }
            }
            bool changed{false};
            for (const auto &p : newPending) {
                bool found{false};
                for (const auto &o : pending) {
                    if (p == o) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    changed = true;
                    break;
                }
            }
            if (changed) {
                pending = newPending;
                pendingIteration = iterationNum;
            }
        }
        if (!pending.empty() && pendingIteration < iterationNum) {
            if (pending.size() == tails.size()) {
                for (int i = 0; i < tails.size(); i++) {
                    watching[i] = pending[i];
                    tails[i].close();
                    tails[i].open(watching[i], std::ios::in);
                }
                pending = {};
            } else {
                typeof(pending.size()) notStaleCounts = 0;
                for (auto &icount : iterationCount) {
                    if (icount >= pendingIteration) {
                        ++notStaleCounts;
                    }
                }
                if (notStaleCounts >= (tails.size() - pending.size())) {
                    int j = 0;
                    for (int i = 0; i < tails.size(); i++) {
                        if (j >= pending.size()) {
                            break;
                        }
                        auto icount = iterationCount[i];
                        if (icount < pendingIteration) {
                            watching[i] = pending[j++];
                            tails[i].close();
                            tails[i].open(watching[i], std::ios::in);
                        }
                    }
                }
            }
        }
        auto watchIterator = watching.begin();
        auto tailIterator = tails.begin();
        auto posIterator = pos.begin();
        auto iterationCountIterator = iterationCount.begin();
        typeof(num) i = 0;
        while (tailIterator != tails.end()) {
            auto lnstart = (LINES * i) / num;
            std::string capt{"===["};
            capt.append(*watchIterator);
            capt.append("]");
            capt.resize(COLS, '=');
            mvaddnstr(lnstart, 0, capt.c_str(), capt.size());
            ++lnstart;
            auto lnend = (LINES * (i + 1)) / num;
            auto &pos = *posIterator;
            auto &fstream = *tailIterator;
            auto curpos = fstream.tellg();
            fstream.seekg(0, std::ios::end);
            auto endpos = fstream.tellg();
            fstream.seekg(curpos, std::ios::beg);
            if (curpos < endpos) {
                inputPrevious = true;
                std::string buf{};
                {
                    auto size = endpos - curpos;
                    if (size > 1024) {
                        size = 1024;
                    }
                    buf.resize(size);
                }
                fstream.read(buf.data(), buf.size());
                *iterationCountIterator = iterationNum;
                while (!buf.empty()) {
                    auto curln = pos / COLS;
                    auto curlnpos = pos % COLS;
                    auto remainln = COLS - curlnpos;
                    std::string printstr{};
                    if (remainln < buf.size()) {
                        printstr = buf.substr(0, remainln);
                    } else {
                        printstr = buf;
                    }
                    bool newln{false};
                    {
                        auto findslashn = printstr.find('\n');
                        if (findslashn < printstr.size()) {
                            printstr.resize(findslashn);
                            newln = true;
                        }
                    }
                    if (!printstr.empty()) {
                        buf = buf.substr(printstr.size());
                    }
                    if (newln) {
                        buf = buf.substr(1);
                    }
                    mvaddnstr(curln + lnstart, curlnpos, printstr.c_str(), printstr.size());
                    pos += printstr.size();
                    remainln -= printstr.size();
                    if (remainln == 0 || newln) {
                        ++curln;
                        if ((curln + lnstart) >= lnend) {
                            std::string lnbuf{};
                            lnbuf.resize(COLS);
                            for (int ln = lnstart + 1; ln < lnend; ln++) {
                                mvinnstr(ln, 0, lnbuf.data(), lnbuf.size());
                                mvaddnstr(ln - 1, 0, lnbuf.data(), lnbuf.size());
                            }
                            lnbuf.resize(0);
                            lnbuf.resize(COLS, ' ');
                            mvaddnstr(lnend - 1, 0, lnbuf.data(), lnbuf.size());
                            pos = (lnend - lnstart - 1) * COLS;
                        } else {
                            pos += remainln;
                        }
                    }
                }
                refresh();
            }
            i++;
            ++watchIterator;
            ++tailIterator;
            ++posIterator;
            ++iterationCountIterator;
        }
    }
    return 0;
}
