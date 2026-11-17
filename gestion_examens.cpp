// ============================================================================
//  GESTION DES EMPLOIS DU TEMPS DES COMPOSITIONS ET EXAMENS  (Bénin)
//  Utilisateurs : Censeur (lycées/collèges) et Chef Scolarité (universités)
//  Compilation  : g++ -std=c++17 -O2 -o gestion_examens gestion_examens.cpp
//  Windows      : taper "chcp 65001" avant de lancer pour les accents
// ============================================================================
#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

// ------------------------------- STRUCTURES ---------------------------------
struct Salle        { int id; string nom; int capacite; };
struct Classe       { int id; string nom; int effectif; };   // classe ou filière
struct Indispo      { int date; int debut; int fin; };      // indisponibilité d'un enseignant
struct Surveillant  { int id; string nom; string matiere; vector<Indispo> indispos; };  // enseignant
struct Epreuve      { int id; string matiere; int classeId; int duree; }; // durée en minutes
struct Seance       { int id; int epreuveId; int date; int debut; int salleId; vector<int> surveillants; };
// date = AAAAMMJJ (ex : 20261125), debut = minutes depuis minuit (ex : 8h30 -> 510)

// ------------------------------- UTILITAIRES --------------------------------
static string pad(const string& s, size_t w) {           // alignement compatible UTF-8
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++;
    return n >= w ? s : s + string(w - n, ' ');
}

static string nettoyer(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    string r = s.substr(a, b - a + 1);
    replace(r.begin(), r.end(), '|', '/');
    return r;
}

static string lireLigne(const string& invite) {
    cout << invite;
    string s;
    if (!getline(cin, s)) { cout << "\nFin de saisie.\n"; exit(0); }
    return nettoyer(s);
}

static string minuscule(string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

static int lireEntier(const string& invite, int mini, int maxi) {
    while (true) {
        string s = lireLigne(invite);
        try {
            size_t pos = 0;
            int v = stoi(s, &pos);
            if (pos == s.size() && v >= mini && v <= maxi) return v;
        } catch (...) {}
        cout << "  Valeur invalide (attendu : " << mini << " à " << maxi << ").\n";
    }
}

static vector<string> decouper(const string& s, char sep) {
    vector<string> r; string cur; istringstream is(s);
    while (getline(is, cur, sep)) r.push_back(cur);
    return r;
}

// ---- Dates et heures ----
static bool dateValide(int d) {
    int a = d / 10000, m = (d / 100) % 100, j = d % 100;
    tm t{}; t.tm_year = a - 1900; t.tm_mon = m - 1; t.tm_mday = j; t.tm_hour = 12;
    if (a < 2000 || a > 2100) return false;
    mktime(&t);
    return t.tm_year == a - 1900 && t.tm_mon == m - 1 && t.tm_mday == j;
}
static string fmtDate(int d) {
    char b[16]; snprintf(b, sizeof b, "%02d/%02d/%04d", d % 100, (d / 100) % 100, d / 10000);
    return b;
}
static int ajouterJours(int d, int n) {
    tm t{}; t.tm_year = d / 10000 - 1900; t.tm_mon = (d / 100) % 100 - 1;
    t.tm_mday = d % 100 + n; t.tm_hour = 12;
    mktime(&t);
    return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
}
static int jourSemaine(int d) {                           // 0 = dimanche
    tm t{}; t.tm_year = d / 10000 - 1900; t.tm_mon = (d / 100) % 100 - 1;
    t.tm_mday = d % 100; t.tm_hour = 12;
    mktime(&t);
    return t.tm_wday;
}
static const char* nomJour(int d) {
    static const char* j[] = {"Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"};
    return j[jourSemaine(d)];
}
static string fmtHeure(int m) {
    char b[24]; snprintf(b, sizeof b, "%02d:%02d", m / 60, m % 60);
    return b;
}
static int lireDate(const string& invite) {
    while (true) {
        string s = lireLigne(invite + " (JJ/MM/AAAA) : ");
        int j, m, a;
        if (sscanf(s.c_str(), "%d/%d/%d", &j, &m, &a) == 3) {
            int d = a * 10000 + m * 100 + j;
            if (dateValide(d)) return d;
        }
        cout << "  Date invalide.\n";
    }
}
static int lireHeure(const string& invite) {
    while (true) {
        string s = lireLigne(invite + " (HH:MM) : ");
        int h, m;
        if ((sscanf(s.c_str(), "%d:%d", &h, &m) == 2 || sscanf(s.c_str(), "%dh%d", &h, &m) == 2)
            && h >= 0 && h < 24 && m >= 0 && m < 60) return h * 60 + m;
        cout << "  Heure invalide.\n";
    }
}

template <class T> int nouvelId(const vector<T>& v) {
    int m = 0; for (auto& x : v) m = max(m, x.id); return m + 1;
}
template <class T> const T* chercher(const vector<T>& v, int id) {
    for (auto& x : v) if (x.id == id) return &x;
    return nullptr;
}

// ------------------------------- SYSTEME ------------------------------------
class Systeme {
public:
    string etablissement = "Établissement";
    string titre = "Compositions du 1er semestre";
    int role = 1;                                  // 1 = Censeur, 2 = Chef Scolarité
    vector<Salle> salles;
    vector<Classe> classes;
    vector<Surveillant> surveillants;
    vector<Epreuve> epreuves;
    vector<Seance> seances;
    const string fichier = "donnees_examens.txt";

    // Vocabulaire selon le profil
    string mGroupe()  const { return role == 1 ? "Classe" : "Filière"; }
    string mEpreuve() const { return role == 1 ? "Composition" : "Examen"; }
    string mSurv()    const { return role == 1 ? "Surveillant" : "Surveillant/Enseignant"; }

    // ----- Calculs -----
    int besoinSurveillants(int effectif) const { return max(1, (effectif + 39) / 40); }  // 1 pour 40 candidats
    int finSeance(const Seance& s) const {
        const Epreuve* e = chercher(epreuves, s.epreuveId);
        return s.debut + (e ? e->duree : 0);
    }
    bool programmee(int epreuveId) const {
        for (auto& s : seances) if (s.epreuveId == epreuveId) return true;
        return false;
    }
    string nomEpreuve(int id) const {
        const Epreuve* e = chercher(epreuves, id);
        return e ? e->matiere : "?";
    }
    string nomClasse(int id) const {
        const Classe* c = chercher(classes, id);
        return c ? c->nom : "?";
    }
    string nomSalle(int id) const {
        const Salle* s = chercher(salles, id);
        return s ? s->nom : "?";
    }
    string nomSurv(int id) const {
        const Surveillant* s = chercher(surveillants, id);
        return s ? s->nom : "?";
    }

    // ----- Détection des conflits -----
    vector<string> conflits(const Seance& s, int ignorerId = -1) const {
        vector<string> res;
        const Epreuve* e = chercher(epreuves, s.epreuveId);
        const Salle* sa = chercher(salles, s.salleId);
        if (!e || !sa) { res.push_back("Épreuve ou salle inexistante."); return res; }
        const Classe* c = chercher(classes, e->classeId);
        int fin = s.debut + e->duree;

        if (c && c->effectif > sa->capacite)
            res.push_back("Capacité insuffisante : " + sa->nom + " (" + to_string(sa->capacite) +
                          " places) pour " + to_string(c->effectif) + " candidats.");
        if (c && (int)s.surveillants.size() < besoinSurveillants(c->effectif))
            res.push_back("Surveillants insuffisants : " + to_string(s.surveillants.size()) + " pour " +
                          to_string(besoinSurveillants(c->effectif)) + " requis (1 pour 40 candidats).");
        if (fin > 24 * 60) res.push_back("L'épreuve dépasse minuit.");
        for (int sv : s.surveillants) {
            const Surveillant* t = chercher(surveillants, sv);
            if (!t) continue;
            if (!t->matiere.empty() && minuscule(t->matiere) == minuscule(e->matiere))
                res.push_back(t->nom + " enseigne " + e->matiere + " : il ne peut pas surveiller sa propre matière.");
            for (auto& iv : t->indispos)
                if (iv.date == s.date && s.debut < iv.fin && iv.debut < fin)
                    res.push_back(t->nom + " est indisponible (" + fmtHeure(iv.debut) + "-" + fmtHeure(iv.fin) + ").");
        }

        for (auto& o : seances) {
            if (o.id == ignorerId || o.date != s.date) continue;
            const Epreuve* eo = chercher(epreuves, o.epreuveId);
            if (!eo) continue;
            int finO = o.debut + eo->duree;
            if (!(s.debut < finO && o.debut < fin)) continue;        // pas de chevauchement
            string ou = " (" + eo->matiere + " " + fmtHeure(o.debut) + "-" + fmtHeure(finO) + ")";
            if (o.salleId == s.salleId) res.push_back("Salle déjà occupée" + ou + ".");
            if (eo->classeId == e->classeId)
                res.push_back(mGroupe() + " déjà en épreuve" + ou + ".");
            for (int sv : s.surveillants)
                if (find(o.surveillants.begin(), o.surveillants.end(), sv) != o.surveillants.end())
                    res.push_back("Surveillant " + nomSurv(sv) + " déjà mobilisé" + ou + ".");
        }
        return res;
    }

    bool survLibre(int svId, int date, int debut, int fin, const string& matiere) const {
        const Surveillant* t = chercher(surveillants, svId);
        if (!t) return false;
        if (!t->matiere.empty() && minuscule(t->matiere) == minuscule(matiere)) return false;
        for (auto& iv : t->indispos) if (iv.date == date && debut < iv.fin && iv.debut < fin) return false;
        for (auto& o : seances) {
            if (o.date != date) continue;
            if (find(o.surveillants.begin(), o.surveillants.end(), svId) == o.surveillants.end()) continue;
            if (debut < finSeance(o) && o.debut < fin) return false;
        }
        return true;
    }
    int chargeSurv(int svId) const {
        int n = 0;
        for (auto& o : seances)
            if (find(o.surveillants.begin(), o.surveillants.end(), svId) != o.surveillants.end()) n++;
        return n;
    }

    // ----- Affichage -----
    void afficherSeance(const Seance& s) const {
        const Epreuve* e = chercher(epreuves, s.epreuveId);
        string sv;
        for (size_t i = 0; i < s.surveillants.size(); i++) sv += (i ? ", " : "") + nomSurv(s.surveillants[i]);
        cout << " " << pad(to_string(s.id), 4)
             << pad(fmtHeure(s.debut) + "-" + fmtHeure(finSeance(s)), 13)
             << pad(e ? e->matiere : "?", 24)
             << pad(e ? nomClasse(e->classeId) : "?", 14)
             << pad(nomSalle(s.salleId), 12) << sv << "\n";
    }

    vector<Seance> triees() const {
        vector<Seance> v = seances;
        sort(v.begin(), v.end(), [](const Seance& a, const Seance& b) {
            return a.date != b.date ? a.date < b.date : a.debut < b.debut;
        });
        return v;
    }

    void entete() const {
        cout << " " << pad("ID", 4) << pad("Horaire", 13) << pad(mEpreuve(), 24)
             << pad(mGroupe(), 14) << pad("Salle", 12) << mSurv() << "\n";
        cout << " " << string(90, '-') << "\n";
    }

    void afficherPlanning(int filtreClasse = -1) const {
        cout << "\n=== " << titre << " - " << etablissement << " ===\n";
        int dateCourante = 0; bool vide = true;
        for (auto& s : triees()) {
            const Epreuve* e = chercher(epreuves, s.epreuveId);
            if (!e || (filtreClasse != -1 && e->classeId != filtreClasse)) continue;
            vide = false;
            if (s.date != dateCourante) {
                dateCourante = s.date;
                cout << "\n>> " << nomJour(s.date) << " " << fmtDate(s.date) << "\n";
                entete();
            }
            afficherSeance(s);
        }
        if (vide) cout << "\n(aucune épreuve programmée)\n";
    }

    void exporter(const string& nom) const {
        ofstream f(nom);
        if (!f) { cout << "Impossible d'écrire " << nom << "\n"; return; }
        streambuf* ancien = cout.rdbuf(f.rdbuf());     // on redirige l'affichage vers le fichier
        afficherPlanning();
        cout << "\n\nLe Censeur / Chef Scolarité : ____________________\n";
        cout.rdbuf(ancien);
        cout << "Planning exporté dans « " << nom << " ».\n";
    }

    static string csvEsc(const string& s) {
        string r = "\"";
        for (char c : s) { if (c == '"') r += "\"\""; else r += c; }
        return r + "\"";
    }
    static string htmlEsc(const string& s) {
        string r;
        for (char c : s) { if (c == '<') r += "&lt;"; else if (c == '>') r += "&gt;"; else if (c == '&') r += "&amp;"; else r += c; }
        return r;
    }
    string listeSurv(const Seance& s) const {
        string r;
        for (size_t i = 0; i < s.surveillants.size(); i++) r += (i ? ", " : "") + nomSurv(s.surveillants[i]);
        return r;
    }
    // CSV (séparateur « ; » + BOM UTF-8 : s'ouvre directement dans Excel)
    void exporterCSV(const string& nom) const {
        ofstream f(nom, ios::binary);
        if (!f) { cout << "Impossible d'écrire " << nom << "\n"; return; }
        f << "\xEF\xBB\xBF" << "Date;Jour;Début;Fin;" << mEpreuve() << ";" << mGroupe() << ";Effectif;Salle;Surveillants\n";
        for (auto& s : triees()) {
            const Epreuve* e = chercher(epreuves, s.epreuveId);
            const Classe* c = e ? chercher(classes, e->classeId) : nullptr;
            if (!e || !c) continue;
            f << fmtDate(s.date) << ";" << nomJour(s.date) << ";" << fmtHeure(s.debut) << ";" << fmtHeure(finSeance(s)) << ";"
              << csvEsc(e->matiere) << ";" << csvEsc(c->nom) << ";" << c->effectif << ";" << csvEsc(nomSalle(s.salleId)) << ";"
              << csvEsc(listeSurv(s)) << "\n";
        }
        cout << "Fichier Excel/CSV créé : « " << nom << " ».\n";
    }
    // Page HTML prête à imprimer : ouvrir dans le navigateur puis Ctrl+P > « Enregistrer au format PDF »
    void exporterHTML(const string& nom) const {
        ofstream f(nom);
        if (!f) { cout << "Impossible d'écrire " << nom << "\n"; return; }
        f << "<!doctype html><html lang=\"fr\"><head><meta charset=\"utf-8\"><title>" << htmlEsc(titre) << "</title><style>"
          << "body{font-family:Arial,sans-serif;margin:24px}h1,h2{text-align:center;margin:4px}h3{margin:18px 0 6px}"
          << "table{width:100%;border-collapse:collapse;font-size:13px}th,td{border:1px solid #444;padding:5px 8px;text-align:left}"
          << "th{background:#e8e8e8}.sig{margin-top:40px;display:flex;justify-content:space-between}"
          << "@media print{h3{break-after:avoid}tr{break-inside:avoid}}</style></head><body>"
          << "<p style=\"text-align:center\">RÉPUBLIQUE DU BÉNIN</p><h1>" << htmlEsc(etablissement) << "</h1><h2>" << htmlEsc(titre) << "</h2>";
        int courante = 0;
        for (auto& s : triees()) {
            const Epreuve* e = chercher(epreuves, s.epreuveId);
            if (!e) continue;
            if (s.date != courante) {
                if (courante) f << "</table>";
                courante = s.date;
                f << "<h3>" << nomJour(s.date) << " " << fmtDate(s.date) << "</h3><table><tr><th>Horaire</th><th>" << mEpreuve()
                  << "</th><th>" << mGroupe() << "</th><th>Salle</th><th>Surveillants</th></tr>";
            }
            f << "<tr><td>" << fmtHeure(s.debut) << " - " << fmtHeure(finSeance(s)) << "</td><td>" << htmlEsc(e->matiere) << "</td><td>"
              << htmlEsc(nomClasse(e->classeId)) << "</td><td>" << htmlEsc(nomSalle(s.salleId)) << "</td><td>" << htmlEsc(listeSurv(s)) << "</td></tr>";
        }
        if (courante) f << "</table>";
        f << "<div class=\"sig\"><span>Le " << (role == 1 ? "Censeur" : "Chef Scolarité") << "</span><span>Le Chef d'établissement</span></div></body></html>";
        cout << "Page imprimable créée : « " << nom << " » (ouvrir puis Ctrl+P > Enregistrer en PDF).\n";
    }

    // ----- Vérification globale -----
    int verifierTout(bool afficher = true) const {
        int total = 0;
        for (auto& s : seances) {
            auto c = conflits(s, s.id);
            for (auto& m : c) {
                total++;
                if (afficher)
                    cout << " [Séance " << s.id << " - " << nomEpreuve(s.epreuveId) << " "
                         << fmtDate(s.date) << "] " << m << "\n";
            }
        }
        if (afficher) cout << (total ? "\n" + to_string(total) + " problème(s) détecté(s).\n"
                                     : "\nAucun conflit : le planning est cohérent.\n");
        return total;
    }

    // ----- Génération automatique -----
    void generer() {
        vector<int> aFaire;
        for (auto& e : epreuves) if (!programmee(e.id)) aFaire.push_back(e.id);
        if (aFaire.empty()) { cout << "Toutes les épreuves sont déjà programmées.\n"; return; }
        if (salles.empty() || surveillants.empty()) { cout << "Ajoutez d'abord des salles et des surveillants.\n"; return; }

        int debutP = lireDate("Premier jour des épreuves");
        int finP = lireDate("Dernier jour des épreuves");
        int maxJour = lireEntier("Nombre max d'épreuves par " + mGroupe() + " et par jour (1-4) : ", 1, 4);
        int hDebut = lireHeure("Heure de début des épreuves");
        int hFin = lireHeure("Heure limite de fin");

        // Les plus gros effectifs d'abord (plus difficiles à placer)
        sort(aFaire.begin(), aFaire.end(), [&](int a, int b) {
            return chercher(classes, chercher(epreuves, a)->classeId)->effectif >
                   chercher(classes, chercher(epreuves, b)->classeId)->effectif;
        });
        vector<Salle> sallesTriees = salles;
        sort(sallesTriees.begin(), sallesTriees.end(), [](const Salle& a, const Salle& b) { return a.capacite < b.capacite; });

        int ok = 0; vector<string> echecs;
        for (int eid : aFaire) {
            const Epreuve* e = chercher(epreuves, eid);
            const Classe* c = chercher(classes, e->classeId);
            bool place = false;
            for (int d = debutP; d <= finP && !place; d = ajouterJours(d, 1)) {
                if (jourSemaine(d) == 0) continue;                       // pas de dimanche
                int nbJour = 0;
                for (auto& o : seances) { const Epreuve* eo = chercher(epreuves, o.epreuveId);
                    if (o.date == d && eo && eo->classeId == e->classeId) nbJour++; }
                if (nbJour >= maxJour) continue;
                for (int h = hDebut; h + e->duree <= hFin && !place; h += 30) {
                    for (auto& sa : sallesTriees) {
                        if (sa.capacite < c->effectif) continue;
                        // surveillants libres, les moins chargés d'abord
                        vector<int> libres;
                        for (auto& sv : surveillants) if (survLibre(sv.id, d, h, h + e->duree, e->matiere)) libres.push_back(sv.id);
                        int besoin = besoinSurveillants(c->effectif);
                        if ((int)libres.size() < besoin) continue;
                        sort(libres.begin(), libres.end(), [&](int a, int b) { return chargeSurv(a) < chargeSurv(b); });
                        Seance s{nouvelId(seances), eid, d, h, sa.id, vector<int>(libres.begin(), libres.begin() + besoin)};
                        if (conflits(s).empty()) { seances.push_back(s); place = true; ok++; break; }
                    }
                }
            }
            if (!place) echecs.push_back(e->matiere + " (" + c->nom + ")");
        }
        cout << "\n" << ok << " épreuve(s) programmée(s) automatiquement.\n";
        if (!echecs.empty()) {
            cout << "Impossible de placer (élargir la période, ajouter salles/surveillants) :\n";
            for (auto& m : echecs) cout << "  - " << m << "\n";
        }
    }

    // ----- Sauvegarde / chargement -----
    void sauvegarder() const {
        ofstream f(fichier);
        if (!f) { cout << "Erreur de sauvegarde.\n"; return; }
        f << "H|" << etablissement << "|" << titre << "|" << role << "\n";
        for (auto& s : salles) f << "S|" << s.id << "|" << s.nom << "|" << s.capacite << "\n";
        for (auto& c : classes) f << "C|" << c.id << "|" << c.nom << "|" << c.effectif << "\n";
        for (auto& s : surveillants) {
            f << "P|" << s.id << "|" << s.nom << "|" << s.matiere << "|";
            for (size_t i = 0; i < s.indispos.size(); i++)
                f << (i ? ";" : "") << s.indispos[i].date << ":" << s.indispos[i].debut << ":" << s.indispos[i].fin;
            f << "\n";
        }
        for (auto& e : epreuves) f << "E|" << e.id << "|" << e.matiere << "|" << e.classeId << "|" << e.duree << "\n";
        for (auto& s : seances) {
            f << "X|" << s.id << "|" << s.epreuveId << "|" << s.date << "|" << s.debut << "|" << s.salleId << "|";
            for (size_t i = 0; i < s.surveillants.size(); i++) f << (i ? "," : "") << s.surveillants[i];
            f << "\n";
        }
    }
    bool charger() {
        ifstream f(fichier);
        if (!f) return false;
        string l;
        try {
            while (getline(f, l)) {
                auto p = decouper(l, '|');
                if (p.empty() || p[0].empty()) continue;
                if (p[0] == "H" && p.size() >= 4) { etablissement = p[1]; titre = p[2]; role = stoi(p[3]); }
                else if (p[0] == "S") salles.push_back({stoi(p[1]), p[2], stoi(p[3])});
                else if (p[0] == "C") classes.push_back({stoi(p[1]), p[2], stoi(p[3])});
                else if (p[0] == "P") {
                    Surveillant t{stoi(p[1]), p[2], p.size() > 3 ? p[3] : "", {}};
                    if (p.size() > 4)
                        for (auto& x : decouper(p[4], ';')) {
                            auto q = decouper(x, ':');
                            if (q.size() == 3) t.indispos.push_back({stoi(q[0]), stoi(q[1]), stoi(q[2])});
                        }
                    surveillants.push_back(t);
                }
                else if (p[0] == "E") epreuves.push_back({stoi(p[1]), p[2], stoi(p[3]), stoi(p[4])});
                else if (p[0] == "X") {
                    Seance s{stoi(p[1]), stoi(p[2]), stoi(p[3]), stoi(p[4]), stoi(p[5]), {}};
                    if (p.size() > 6) for (auto& x : decouper(p[6], ',')) if (!x.empty()) s.surveillants.push_back(stoi(x));
                    seances.push_back(s);
                }
            }
        } catch (...) { cout << "Fichier de données corrompu.\n"; return false; }
        return true;
    }
};

// ------------------------------- MENUS --------------------------------------
static void menuSalles(Systeme& S) {
    while (true) {
        cout << "\n--- SALLES ---\n1. Ajouter\n2. Lister\n3. Supprimer\n0. Retour\n";
        int c = lireEntier("Choix : ", 0, 3);
        if (c == 0) return;
        if (c == 1) {
            string n = lireLigne("Nom de la salle (ex : Amphi A) : ");
            if (n.empty()) { cout << "Nom vide.\n"; continue; }
            int cap = lireEntier("Capacité (places d'examen) : ", 1, 5000);
            S.salles.push_back({nouvelId(S.salles), n, cap});
            cout << "Salle ajoutée.\n";
        } else if (c == 2) {
            for (auto& s : S.salles) cout << " " << pad(to_string(s.id), 4) << pad(s.nom, 22) << s.capacite << " places\n";
            if (S.salles.empty()) cout << " (aucune)\n";
        } else {
            int id = lireEntier("ID à supprimer : ", 1, 1000000);
            bool utilisee = any_of(S.seances.begin(), S.seances.end(), [&](const Seance& s) { return s.salleId == id; });
            if (utilisee) { cout << "Salle utilisée dans le planning : suppression refusée.\n"; continue; }
            auto it = remove_if(S.salles.begin(), S.salles.end(), [&](const Salle& s) { return s.id == id; });
            cout << (it != S.salles.end() ? "Supprimée.\n" : "ID introuvable.\n");
            S.salles.erase(it, S.salles.end());
        }
    }
}

static void menuClasses(Systeme& S) {
    while (true) {
        cout << "\n--- " << S.mGroupe() << "S ---\n1. Ajouter\n2. Lister\n3. Supprimer\n0. Retour\n";
        int c = lireEntier("Choix : ", 0, 3);
        if (c == 0) return;
        if (c == 1) {
            string n = lireLigne("Nom (ex : " + string(S.role == 1 ? "Tle D" : "L2 Informatique") + ") : ");
            if (n.empty()) { cout << "Nom vide.\n"; continue; }
            int eff = lireEntier("Effectif : ", 1, 5000);
            S.classes.push_back({nouvelId(S.classes), n, eff});
            cout << "Ajouté.\n";
        } else if (c == 2) {
            for (auto& x : S.classes) cout << " " << pad(to_string(x.id), 4) << pad(x.nom, 22) << x.effectif << " candidats\n";
            if (S.classes.empty()) cout << " (aucun)\n";
        } else {
            int id = lireEntier("ID à supprimer : ", 1, 1000000);
            bool utilisee = any_of(S.epreuves.begin(), S.epreuves.end(), [&](const Epreuve& e) { return e.classeId == id; });
            if (utilisee) { cout << "Des épreuves dépendent de cet élément : suppression refusée.\n"; continue; }
            auto it = remove_if(S.classes.begin(), S.classes.end(), [&](const Classe& x) { return x.id == id; });
            cout << (it != S.classes.end() ? "Supprimé.\n" : "ID introuvable.\n");
            S.classes.erase(it, S.classes.end());
        }
    }
}

static void menuSurveillants(Systeme& S) {
    while (true) {
        cout << "\n--- ENSEIGNANTS / " << S.mSurv() << "S ---\n1. Ajouter\n2. Lister\n3. Supprimer"
             << "\n4. Ajouter une indisponibilité\n5. Voir / effacer les indisponibilités\n0. Retour\n";
        int c = lireEntier("Choix : ", 0, 5);
        if (c == 0) return;
        if (c == 1) {
            string n = lireLigne("Nom complet : ");
            if (n.empty()) { cout << "Nom vide.\n"; continue; }
            string m = lireLigne("Matière enseignée (vide si aucune) : ");
            S.surveillants.push_back({nouvelId(S.surveillants), n, m, {}});
            cout << "Ajouté. (Il ne surveillera jamais sa propre matière.)\n";
        } else if (c == 2) {
            for (auto& x : S.surveillants)
                cout << " " << pad(to_string(x.id), 4) << pad(x.nom, 26) << pad(x.matiere.empty() ? "-" : x.matiere, 18)
                     << S.chargeSurv(x.id) << " séance(s), " << x.indispos.size() << " indispo.\n";
            if (S.surveillants.empty()) cout << " (aucun)\n";
        } else if (c == 3) {
            int id = lireEntier("ID à supprimer : ", 1, 1000000);
            bool utilise = any_of(S.seances.begin(), S.seances.end(), [&](const Seance& s) {
                return find(s.surveillants.begin(), s.surveillants.end(), id) != s.surveillants.end(); });
            if (utilise) { cout << "Déjà affecté dans le planning : suppression refusée.\n"; continue; }
            auto it = remove_if(S.surveillants.begin(), S.surveillants.end(), [&](const Surveillant& x) { return x.id == id; });
            cout << (it != S.surveillants.end() ? "Supprimé.\n" : "ID introuvable.\n");
            S.surveillants.erase(it, S.surveillants.end());
        } else {
            int id = lireEntier("ID de l'enseignant : ", 1, 1000000);
            Surveillant* t = nullptr;
            for (auto& x : S.surveillants) if (x.id == id) t = &x;
            if (!t) { cout << "ID introuvable.\n"; continue; }
            if (c == 4) {
                int d = lireDate("Date d'indisponibilité");
                int h1 = lireHeure("De"), h2 = lireHeure("À");
                if (h2 <= h1) { cout << "L'heure de fin doit suivre le début.\n"; continue; }
                t->indispos.push_back({d, h1, h2});
                cout << "Indisponibilité enregistrée.\n";
            } else {
                for (auto& iv : t->indispos) cout << "  " << fmtDate(iv.date) << " " << fmtHeure(iv.debut) << "-" << fmtHeure(iv.fin) << "\n";
                if (t->indispos.empty()) { cout << "  (aucune)\n"; continue; }
                if (lireLigne("Tout effacer ? (o/n) : ") == "o") { t->indispos.clear(); cout << "Effacé.\n"; }
            }
        }
    }
}

static void menuEpreuves(Systeme& S) {
    while (true) {
        cout << "\n--- " << S.mEpreuve() << "S ---\n1. Ajouter\n2. Lister\n3. Supprimer\n0. Retour\n";
        int c = lireEntier("Choix : ", 0, 3);
        if (c == 0) return;
        if (c == 1) {
            if (S.classes.empty()) { cout << "Créez d'abord une " << S.mGroupe() << ".\n"; continue; }
            string m = lireLigne("Matière / Unité d'enseignement : ");
            if (m.empty()) { cout << "Nom vide.\n"; continue; }
            for (auto& x : S.classes) cout << "  " << x.id << ". " << x.nom << " (" << x.effectif << ")\n";
            int cid = lireEntier("ID de la " + S.mGroupe() + " : ", 1, 1000000);
            if (!chercher(S.classes, cid)) { cout << "ID introuvable.\n"; continue; }
            int d = lireEntier("Durée en minutes (ex : 120) : ", 15, 480);
            S.epreuves.push_back({nouvelId(S.epreuves), m, cid, d});
            cout << "Ajouté.\n";
        } else if (c == 2) {
            for (auto& e : S.epreuves)
                cout << " " << pad(to_string(e.id), 4) << pad(e.matiere, 26) << pad(S.nomClasse(e.classeId), 14)
                     << pad(to_string(e.duree) + " min", 10) << (S.programmee(e.id) ? "[programmée]" : "[à programmer]") << "\n";
            if (S.epreuves.empty()) cout << " (aucune)\n";
        } else {
            int id = lireEntier("ID à supprimer : ", 1, 1000000);
            if (S.programmee(id)) { cout << "Déjà programmée : supprimez d'abord la séance.\n"; continue; }
            auto it = remove_if(S.epreuves.begin(), S.epreuves.end(), [&](const Epreuve& e) { return e.id == id; });
            cout << (it != S.epreuves.end() ? "Supprimée.\n" : "ID introuvable.\n");
            S.epreuves.erase(it, S.epreuves.end());
        }
    }
}

static void programmerManuel(Systeme& S) {
    vector<const Epreuve*> libres;
    for (auto& e : S.epreuves) if (!S.programmee(e.id)) libres.push_back(&e);
    if (libres.empty()) { cout << "Aucune épreuve à programmer.\n"; return; }
    if (S.salles.empty() || S.surveillants.empty()) { cout << "Ajoutez des salles et des surveillants d'abord.\n"; return; }

    cout << "\nÉpreuves à programmer :\n";
    for (auto e : libres)
        cout << "  " << e->id << ". " << e->matiere << " - " << S.nomClasse(e->classeId) << " (" << e->duree << " min)\n";
    int eid = lireEntier("ID de l'épreuve : ", 1, 1000000);
    const Epreuve* e = chercher(S.epreuves, eid);
    if (!e || S.programmee(eid)) { cout << "ID invalide.\n"; return; }

    Seance s{nouvelId(S.seances), eid, lireDate("Date"), lireHeure("Heure de début"), 0, {}};
    cout << "Salles :\n";
    for (auto& x : S.salles) cout << "  " << x.id << ". " << x.nom << " (" << x.capacite << " places)\n";
    s.salleId = lireEntier("ID de la salle : ", 1, 1000000);
    cout << "Surveillants (besoin : " << S.besoinSurveillants(chercher(S.classes, e->classeId)->effectif) << ") :\n";
    for (auto& x : S.surveillants) cout << "  " << x.id << ". " << x.nom << "\n";
    string ids = lireLigne("IDs séparés par des virgules (ex : 1,3) : ");
    for (auto& t : decouper(ids, ',')) {
        try { int v = stoi(t); if (chercher(S.surveillants, v) &&
              find(s.surveillants.begin(), s.surveillants.end(), v) == s.surveillants.end()) s.surveillants.push_back(v); }
        catch (...) {}
    }
    auto c = S.conflits(s);
    if (!c.empty()) {
        cout << "\nProgrammation REFUSÉE :\n";
        for (auto& m : c) cout << "  - " << m << "\n";
        return;
    }
    S.seances.push_back(s);
    cout << "Épreuve programmée.\n";
}

static void supprimerSeance(Systeme& S) {
    int id = lireEntier("ID de la séance à retirer du planning : ", 1, 1000000);
    auto it = remove_if(S.seances.begin(), S.seances.end(), [&](const Seance& s) { return s.id == id; });
    cout << (it != S.seances.end() ? "Séance retirée (l'épreuve redevient à programmer).\n" : "ID introuvable.\n");
    S.seances.erase(it, S.seances.end());
}

static void modifierSeance(Systeme& S) {
    int id = lireEntier("ID de la séance à modifier : ", 1, 1000000);
    const Seance* o = chercher(S.seances, id);
    if (!o) { cout << "ID introuvable.\n"; return; }
    Seance s = *o;
    cout << "Séance actuelle :\n"; S.entete(); S.afficherSeance(s);
    cout << "(Laissez vide pour conserver la valeur actuelle)\n";

    string t = lireLigne("Nouvelle date JJ/MM/AAAA [" + fmtDate(s.date) + "] : ");
    if (!t.empty()) {
        int j, m, a;
        if (sscanf(t.c_str(), "%d/%d/%d", &j, &m, &a) == 3 && dateValide(a * 10000 + m * 100 + j)) s.date = a * 10000 + m * 100 + j;
        else { cout << "Date invalide : modification annulée.\n"; return; }
    }
    t = lireLigne("Nouvelle heure de début HH:MM [" + fmtHeure(s.debut) + "] : ");
    if (!t.empty()) {
        int h, m;
        if ((sscanf(t.c_str(), "%d:%d", &h, &m) == 2 || sscanf(t.c_str(), "%dh%d", &h, &m) == 2) && h >= 0 && h < 24 && m >= 0 && m < 60) s.debut = h * 60 + m;
        else { cout << "Heure invalide : modification annulée.\n"; return; }
    }
    for (auto& x : S.salles) cout << "  " << x.id << ". " << x.nom << " (" << x.capacite << " places)\n";
    t = lireLigne("Nouvelle salle (ID) [" + S.nomSalle(s.salleId) + "] : ");
    if (!t.empty()) {
        try { s.salleId = stoi(t); } catch (...) { cout << "ID invalide : modification annulée.\n"; return; }
    }
    for (auto& x : S.surveillants) cout << "  " << x.id << ". " << x.nom << (x.matiere.empty() ? "" : " (" + x.matiere + ")") << "\n";
    t = lireLigne("Nouveaux surveillants, IDs séparés par des virgules [" + S.listeSurv(s) + "] : ");
    if (!t.empty()) {
        s.surveillants.clear();
        for (auto& x : decouper(t, ',')) {
            try { int v = stoi(x); if (chercher(S.surveillants, v) &&
                  find(s.surveillants.begin(), s.surveillants.end(), v) == s.surveillants.end()) s.surveillants.push_back(v); }
            catch (...) {}
        }
    }
    auto c = S.conflits(s, s.id);
    if (!c.empty()) {
        cout << "\nModification REFUSÉE (la séance reste inchangée) :\n";
        for (auto& m : c) cout << "  - " << m << "\n";
        return;
    }
    for (auto& x : S.seances) if (x.id == id) x = s;
    cout << "Séance modifiée.\n";
}

static void menuPlanning(Systeme& S) {
    while (true) {
        cout << "\n--- PLANNING ---\n1. Afficher le planning complet\n2. Afficher par " << S.mGroupe()
             << "\n3. Programmer une épreuve (manuel)\n4. Génération automatique\n5. Modifier une séance"
             << "\n6. Retirer une séance\n7. Vérifier les conflits\n8. Exporter en texte"
             << "\n9. Exporter en CSV (Excel)\n10. Exporter en page imprimable (PDF)\n0. Retour\n";
        int c = lireEntier("Choix : ", 0, 10);
        switch (c) {
            case 0: return;
            case 1: S.afficherPlanning(); break;
            case 2: {
                for (auto& x : S.classes) cout << "  " << x.id << ". " << x.nom << "\n";
                int id = lireEntier("ID : ", 1, 1000000);
                S.afficherPlanning(id); break;
            }
            case 3: programmerManuel(S); break;
            case 4: S.generer(); break;
            case 5: modifierSeance(S); break;
            case 6: supprimerSeance(S); break;
            case 7: S.verifierTout(); break;
            case 8: S.exporter("planning_examens.txt"); break;
            case 9: S.exporterCSV("planning_examens.csv"); break;
            case 10: S.exporterHTML("planning_examens.html"); break;
        }
    }
}

// --------------------------------- MAIN -------------------------------------
int main() {
    Systeme S;
    cout << "=====================================================\n"
         << "  GESTION DES EMPLOIS DU TEMPS - COMPOSITIONS/EXAMENS\n"
         << "=====================================================\n";
    bool existant = S.charger();
    if (existant) {
        cout << "Données chargées : " << S.etablissement << " (" << (S.role == 1 ? "Censeur" : "Chef Scolarité") << ")\n";
    } else {
        cout << "Profil :\n  1. Censeur (lycée / collège)\n  2. Chef Scolarité (université)\n";
        S.role = lireEntier("Choix : ", 1, 2);
        S.etablissement = lireLigne("Nom de l'établissement : ");
        S.titre = lireLigne("Intitulé de la session (ex : Compositions du 1er semestre) : ");
    }

    while (true) {
        cout << "\n========== MENU PRINCIPAL ==========\n"
             << "1. Salles\n2. " << S.mGroupe() << "s\n3. " << S.mSurv() << "s\n4. " << S.mEpreuve()
             << "s\n5. Planning\n6. Sauvegarder\n0. Sauvegarder et quitter\n";
        int c = lireEntier("Choix : ", 0, 6);
        switch (c) {
            case 1: menuSalles(S); break;
            case 2: menuClasses(S); break;
            case 3: menuSurveillants(S); break;
            case 4: menuEpreuves(S); break;
            case 5: menuPlanning(S); break;
            case 6: S.sauvegarder(); cout << "Données sauvegardées.\n"; break;
            case 0: S.sauvegarder(); cout << "Données sauvegardées. Au revoir !\n"; return 0;
        }
    }
}
