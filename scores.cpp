// ============================================================================
//  scores.cpp - the top ten, with names, kept in scores.txt next to the .exe
//
//  Each line of scores.txt is a score and a name:   12450 Paul
// ============================================================================

#include "cavern.h"

ScoreEntry scores[NUM_SCORES];

static void SaveScores(void)
{
    char text[NUM_SCORES * 40 + 1];
    int  n = 0;
    for (int i = 0; i < NUM_SCORES && scores[i].score > 0; i++)
        n += wsprintf(text + n, "%d %s\n", scores[i].score, scores[i].name);
    text[n] = 0;

    char path[MAX_PATH];
    ExePath(SCORE_FILE, path);
    WriteTextFile(path, text);
}

// Put a score into the table in order, pushing the lowest off the end
static void Insert(int s, const char *name)
{
    int i = NUM_SCORES - 1;
    if (s <= scores[i].score)
        return;
    while (i > 0 && s > scores[i - 1].score)
    {
        scores[i] = scores[i - 1];
        i--;
    }
    scores[i].score = s;
    lstrcpyn(scores[i].name, (name && name[0]) ? name : "Anon", sizeof scores[i].name);
}

void LoadScores(void)
{
    char path[MAX_PATH], line[100];
    memset(scores, 0, sizeof scores);
    ExePath(SCORE_FILE, path);

    char *text = ReadTextFile(path);
    if (text)
    {
        const char *p = text;
        while (*p)
        {
            p = NextLine(p, line, sizeof line);
            char *name;
            long s = strtol(line, &name, 10);
            while (*name == ' ' || *name == '\t') name++;
            if (s > 0)
                Insert((int)s, name);
        }
        free(text);
        return;
    }

    // No table yet: bring over the single high score that older versions kept
    // in a .hi file named after the .exe
    GetModuleFileName(NULL, path, MAX_PATH);
    char *dot = strrchr(path, '.');
    if (dot) lstrcpy(dot, ".hi");
    HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE)
    {
        int v = 0;
        DWORD got = 0;
        if (ReadFile(h, &v, sizeof v, &got, NULL) && got == sizeof v && v > 0)
        {
            Insert(v, "Earlier best");
            SaveScores();
        }
        CloseHandle(h);
    }
}

int HiScore(void)
{
    return scores[0].score;
}

BOOL ScoreQualifies(int s)
{
    return s > 0 && s > scores[NUM_SCORES - 1].score;
}

void AddScore(int s, const char *name)
{
    Insert(s, name);
    SaveScores();
}
