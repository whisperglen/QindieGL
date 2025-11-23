// QindieGL_Tests.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include "tests.h"

static int tests_total = 0;
static int tests_ok = 0;

extern void do_texgen_tests();

int main()
{
    printf("QindieGL Tests\n");

    do_texgen_tests();

    printf("Tests results: %d/%d\n", tests_ok, tests_total);
    system("pause");
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file


void xassert_str(int success, const char* expression, const char* function, unsigned line, const char* file)
{
    tests_total++;
    if (success) tests_ok++;
    else
    {
        const char* fn = strrchr(file, '\\');
        if (!fn) fn = strrchr(file, '/');
        if (!fn) fn = "fnf";

        printf("assert failed: %s in %s:%d %s\n", expression, function, line, fn);
    }
}

void xassert_int(int success, int printval, const char* function, unsigned line, const char* file)
{
    tests_total++;
    if (success) tests_ok++;
    else
    {
        const char* fn = strrchr(file, '\\');
        if (!fn) fn = strrchr(file, '/');
        if (!fn) fn = "fnf";

        printf("assert failed: 0x%x in %s:%d %s\n", printval, function, line, fn);
    }
}

int ispasswd(int val)
{
    return isalnum(val) || ispunct(val);
}

void random_init()
{
    static int initialised = 0;
    if (initialised == 0)
    {
        unsigned int seed = (unsigned int)time(NULL);
        srand(seed);
        initialised = 1;
        printf("seed: %d\n", seed);
    }
}

void random_bytes(uc8_t* out, int size)
{
    random_init();

    int i;
    for (i = 0; i < size; i++)
    {
        out[i] = rand() % 0x100;
    }
}

void random_text(uc8_t* out, int size)
{
    random_init();

    int i;
    for (i = 0; i < size; )
    {
        int val = rand();
        uc8_t* itr = (uc8_t*)&val;
        for (int j = 0; j < sizeof(val); j++, itr++)
        {
            if (ispasswd(*itr))
            {
                out[i] = *itr;
                i++;
                break;
            }
        }
    }
}