/*
  Cash Flow Optimizer 
  ----------------------------------------------------------
  - Purpose: Given group transactions, compute net balances and produce a minimum-settlement
    list of payments to clear all debts using a greedy algorithm.
  - Team split: This file is intentionally split into three labeled parts for contributors A, B, C.
    Each part contains functions and responsibilities which the respective team member owns.
  - Style: Written in an "intermediate" C style (modular, moderate comments, not overly annotated).

  NOTE: This file also includes a slide-deck draft near the end (inside a big comment block).

  Parts:
    Virag Desai: Data structures, hash map, transaction parsing, file I/O
    Vikhyat Depura: Graph representation, greedy settlement algorithm, payment generation
    Jayraj Choughule: User interface (menu), reports, history management, sample dataset

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ----------------------------- Common typedefs ----------------------------- */
#define MAX_NAME_LEN 64
#define INITIAL_USER_CAP 32
#define HASHMAP_SIZE 101
#define MAX_TRANSACTIONS 1024

typedef struct User {
    int id;                 // internal id
    char name[MAX_NAME_LEN];
} User;

typedef struct Transaction {
    int payer_id;          // who paid
    int *participants;     // array of participant ids (including payer if applicable)
    int part_count;        // number of participants
    double amount;         // total amount paid
    char note[128];
} Transaction;

/* ----------------------------- PART A - Virag Desai -----------------------------
   Responsibilities:
   - Manage users array and hashmap for balances
   - Read/write simple transaction files
   - Utilities for user lookup and management
*/

// ---------------- Hash map for net balances (simple separate chaining) ----------------

typedef struct BalanceNode {
    int user_id;
    double balance; // positive => others owe this user; negative => user owes others
    struct BalanceNode *next;
} BalanceNode;

typedef struct BalanceMap {
    BalanceNode *buckets[HASHMAP_SIZE];
} BalanceMap;

static unsigned int hash_int(int x) {
    unsigned int h = (unsigned int)x;
    h = ((h >> 16) ^ h) * 0x45d9f3b;
    h = ((h >> 16) ^ h) * 0x45d9f3b;
    h = (h >> 16) ^ h;
    return h % HASHMAP_SIZE;
}

BalanceMap *bm_create() {
    BalanceMap *m = malloc(sizeof(BalanceMap));
    if (!m) { perror("malloc"); exit(1); }
    for (int i=0;i<HASHMAP_SIZE;i++) m->buckets[i]=NULL;
    return m;
}

void bm_add(BalanceMap *m, int user_id, double delta) {
    unsigned int h = hash_int(user_id);
    BalanceNode *cur = m->buckets[h];
    while (cur) {
        if (cur->user_id == user_id) { cur->balance += delta; return; }
        cur = cur->next;
    }
    BalanceNode *n = malloc(sizeof(BalanceNode));
    n->user_id = user_id; n->balance = delta; n->next = m->buckets[h];
    m->buckets[h] = n;
}

double bm_get(BalanceMap *m, int user_id) {
    unsigned int h = hash_int(user_id);
    BalanceNode *cur = m->buckets[h];
    while (cur) {
        if (cur->user_id == user_id) return cur->balance;
        cur = cur->next;
    }
    return 0.0;
}

void bm_set(BalanceMap *m, int user_id, double value) {
    unsigned int h = hash_int(user_id);
    BalanceNode *cur = m->buckets[h];
    while (cur) {
        if (cur->user_id == user_id) { cur->balance = value; return; }
        cur = cur->next;
    }
    BalanceNode *n = malloc(sizeof(BalanceNode));
    n->user_id = user_id; n->balance = value; n->next = m->buckets[h];
    m->buckets[h] = n;
}

void bm_free(BalanceMap *m) {
    for (int i=0;i<HASHMAP_SIZE;i++) {
        BalanceNode *cur = m->buckets[i];
        while (cur) { BalanceNode *t = cur; cur = cur->next; free(t); }
    }
    free(m);
}

// ---------------- User registry ----------------

typedef struct UserRegistry {
    User *users;
    int capacity;
    int count;
} UserRegistry;

UserRegistry *ur_create() {
    UserRegistry *ur = malloc(sizeof(UserRegistry));
    ur->capacity = INITIAL_USER_CAP;
    ur->count = 0;
    ur->users = malloc(sizeof(User)*ur->capacity);
    return ur;
}

int ur_find_by_name(UserRegistry *ur, const char *name) {
    for (int i=0;i<ur->count;i++) {
        if (strcmp(ur->users[i].name, name)==0) return ur->users[i].id;
    }
    return -1;
}

int ur_add(UserRegistry *ur, const char *name) {
    int existing = ur_find_by_name(ur, name);
    if (existing >= 0) return existing;
    if (ur->count >= ur->capacity) {
        ur->capacity *= 2;
        ur->users = realloc(ur->users, sizeof(User)*ur->capacity);
    }
    int id = ur->count;
    ur->users[ur->count].id = id;
    strncpy(ur->users[ur->count].name, name, MAX_NAME_LEN-1);
    ur->users[ur->count].name[MAX_NAME_LEN-1]='\0';
    ur->count++;
    return id;
}

const char *ur_name(UserRegistry *ur, int id) {
    if (id<0 || id>=ur->count) return "<unknown>";
    return ur->users[id].name;
}

void ur_free(UserRegistry *ur) {
    if (ur) { free(ur->users); free(ur); }
}

// ---------------- Transaction list management ----------------

typedef struct TransactionList {
    Transaction items[MAX_TRANSACTIONS];
    int count;
} TransactionList;

TransactionList *tl_create() {
    TransactionList *tl = malloc(sizeof(TransactionList));
    tl->count = 0;
    return tl;
}

void tl_add(TransactionList *tl, Transaction t) {
    if (tl->count >= MAX_TRANSACTIONS) {
        printf("Reached max transactions.\n"); return;
    }
    tl->items[tl->count++] = t;
}

void tl_free(TransactionList *tl) { if (tl) free(tl); }

// Simple helper to create transaction from names
Transaction make_transaction(UserRegistry *ur, const char *payer, char **parts, int pcount, double amount, const char *note) {
    Transaction t;
    int pid = ur_find_by_name(ur, payer);
    if (pid == -1) pid = ur_add(ur, payer);
    t.payer_id = pid;
    t.part_count = pcount;
    t.participants = malloc(sizeof(int)*pcount);
    for (int i=0;i<pcount;i++) {
        int uid = ur_find_by_name(ur, parts[i]);
        if (uid==-1) uid = ur_add(ur, parts[i]);
        t.participants[i] = uid;
    }
    t.amount = amount;
    strncpy(t.note, note, sizeof(t.note)-1);
    t.note[sizeof(t.note)-1]='\0';
    return t;
}

void transaction_free(Transaction *t) { if (t && t->participants) free(t->participants); }

// ---------------- Simple file I/O for transactions ----------------
// A lightweight, line-based format (not robust but easy to parse)
// Format per line: payer|amount|participant1,participant2,...|note

int save_transactions(const char *filename, TransactionList *tl, UserRegistry *ur) {
    FILE *f = fopen(filename, "w");
    if (!f) { perror("fopen"); return 0; }
    // write users
    fprintf(f, "#USERS\n");
    for (int i=0;i<ur->count;i++) {
        fprintf(f, "%d|%s\n", ur->users[i].id, ur->users[i].name);
    }
    fprintf(f, "#TRANSACTIONS\n");
    for (int i=0;i<tl->count;i++) {
        Transaction *t = &tl->items[i];
        fprintf(f, "%s|%.2f|", ur_name(ur, t->payer_id), t->amount);
        for (int j=0;j<t->part_count;j++) {
            fprintf(f, "%s", ur_name(ur, t->participants[j]));
            if (j<t->part_count-1) fprintf(f, ",");
        }
        fprintf(f, "|%s\n", t->note);
    }
    fclose(f);
    return 1;
}

int load_transactions(const char *filename, TransactionList *tl, UserRegistry *ur) {
    FILE *f = fopen(filename, "r");
    if (!f) { perror("fopen"); return 0; }
    char line[512];
    enum {READ_USERS, READ_TRAN} mode = READ_USERS;
    while (fgets(line, sizeof(line), f)) {
        if (line[0]=='#') {
            if (strncmp(line, "#USERS",6)==0) mode = READ_USERS;
            else if (strncmp(line, "#TRANSACTIONS",13)==0) mode = READ_TRAN;
            continue;
        }
        char *s = line;
        // trim newline
        char *nl = strchr(s,'\n'); if (nl) *nl='\0';
        if (mode == READ_USERS) {
            // id|name
            char *pipe = strchr(s,'|');
            if (!pipe) continue;
            *pipe='\0';
            int id = atoi(s);
            char *name = pipe+1;
            // ensure registry size
            int exists = ur_find_by_name(ur, name);
            if (exists==-1) {
                int added = ur_add(ur, name);
                // try to match id if simple
                (void)added; // we keep the current id scheme (sequential)
            }
        } else if (mode == READ_TRAN) {
            // payer|amount|p1,p2,...|note
            char *p1 = strchr(s,'|'); if (!p1) continue; *p1='\0';
            char *p2 = strchr(p1+1,'|'); if (!p2) continue; *p2='\0';
            char *p3 = strchr(p2+1,'|'); if (!p3) continue; *p3='\0';
            char *payer = s;
            double amount = atof(p1+1);
            char *plist = p2+1;
            char *note = p3+1;
            // split participants by comma
            char *token;
            int parts=0;
            char *parts_arr[64];
            token = strtok(plist, ",");
            while (token && parts<64) { // participants max 64 per tran
                // trim spaces
                while(isspace((unsigned char)*token)) token++;
                parts_arr[parts++] = token;
                token = strtok(NULL, ",");
            }
            Transaction t = make_transaction(ur, payer, parts_arr, parts, amount, note);
            tl_add(tl, t);
        }
    }
    fclose(f);
    return 1;
}

/* ----------------------------- PART B - Vikhyat Depura -----------------------------
   Responsibilities:
   - Build graph of debts from net balances
   - Implement greedy settlement algorithm (largest creditor with largest debtor)
   - Maintain payment history and produce settlement list
*/

// ---------------- Graph representation ----------------

// For this project, we represent the final debts as a list of directed edges (debtor -> creditor : amount).

typedef struct DebtEdge {
    int from; // debtor id
    int to;   // creditor id
    double amount;
} DebtEdge;

typedef struct DebtList {
    DebtEdge *edges;
    int count;
    int capacity;
} DebtList;

DebtList *dl_create(int cap) {
    DebtList *dl = malloc(sizeof(DebtList));
    dl->capacity = cap>0?cap:16;
    dl->count = 0;
    dl->edges = malloc(sizeof(DebtEdge)*dl->capacity);
    return dl;
}

void dl_add(DebtList *dl, int from, int to, double amount) {
    if (dl->count >= dl->capacity) {
        dl->capacity *= 2;
        dl->edges = realloc(dl->edges, sizeof(DebtEdge)*dl->capacity);
    }
    dl->edges[dl->count].from = from;
    dl->edges[dl->count].to = to;
    dl->edges[dl->count].amount = amount;
    dl->count++;
}

void dl_free(DebtList *dl) { if (dl) { free(dl->edges); free(dl); } }

// ---------------- Build net balances from transactions ----------------

void compute_net_balances(TransactionList *tl, UserRegistry *ur, BalanceMap *bm) {
    // start with zero balances
    // For every transaction: payer pays amount that should be split equally among participants
    // so each participant owes share. Payer effectively pays for others too.
    for (int i=0;i<tl->count;i++) {
        Transaction *t = &tl->items[i];
        double share = t->amount / t->part_count;
        for (int j=0;j<t->part_count;j++) {
            int uid = t->participants[j];
            if (uid == t->payer_id) {
                // payer's net change: paid full amount but consumes share
                bm_add(bm, uid, t->amount - share);
            } else {
                // this participant owes share
                bm_add(bm, uid, -share);
            }
        }
    }
}

// ---------------- Greedy settlement algorithm ----------------
// Convert balance map to two arrays: debtors (negative) and creditors (positive).

typedef struct BalPair { int id; double bal; } BalPair;

int compare_bal_desc(const void *a, const void *b) {
    double da = ((BalPair*)a)->bal;
    double db = ((BalPair*)b)->bal;
    if (da < db) return 1; if (da > db) return -1; return 0;
}

DebtList *settle_greedy(BalanceMap *bm, UserRegistry *ur) {
    // gather all balances into arrays
    BalPair *creds = malloc(sizeof(BalPair)*ur->count);
    BalPair *debs = malloc(sizeof(BalPair)*ur->count);
    int nc=0, nd=0;
    for (int i=0;i<ur->count;i++) {
        double v = bm_get(bm, ur->users[i].id);
        if (v > 1e-9) { creds[nc].id = ur->users[i].id; creds[nc].bal = v; nc++; }
        else if (v < -1e-9) { debs[nd].id = ur->users[i].id; debs[nd].bal = -v; nd++; } // store positive owed amount
    }
    // sort creditors descending by amount (largest first) and debtors descending
    qsort(creds, nc, sizeof(BalPair), compare_bal_desc);
    qsort(debs, nd, sizeof(BalPair), compare_bal_desc);
    DebtList *res = dl_create(16);
    int i=0, j=0;
    while (i<nd && j<nc) {
        double owe = debs[i].bal;
        double need = creds[j].bal;
        double pay = owe < need ? owe : need;
        dl_add(res, debs[i].id, creds[j].id, pay);
        debs[i].bal -= pay;
        creds[j].bal -= pay;
        if (debs[i].bal <= 1e-9) i++;
        if (creds[j].bal <= 1e-9) j++;
    }
    free(creds); free(debs);
    return res;
}

// ---------------- Helper to print settlement ----------------

void print_settlement(DebtList *dl, UserRegistry *ur) {
    printf("\nSuggested settlements (minimized payments):\n");
    for (int i=0;i<dl->count;i++) {
        DebtEdge *e = &dl->edges[i];
        printf(" - %s pays %s : Rs %.2f\n", ur_name(ur, e->from), ur_name(ur, e->to), e->amount);
    }
}

// ---------------- Optional: Build explicit graph edges from pairwise debts ----------------
// For traceability, this produces a list of directed edges with non-zero amounts.

DebtList *build_graph_from_balances(BalanceMap *bm, UserRegistry *ur) {
    DebtList *dl = dl_create(ur->count);
    // naive approach: for educational purposes only - convert balances to pairwise edges using greedy settlement
    DebtList *settle = settle_greedy(bm, ur);
    // copy
    for (int i=0;i<settle->count;i++) dl_add(dl, settle->edges[i].from, settle->edges[i].to, settle->edges[i].amount);
    dl_free(settle);
    return dl;
}

/* ----------------------------- PART C - Jayraj Choughule -----------------------------
   Responsibilities:
   - Menu-driven interface
   - Reports and history tracking
   - Sample dataset and testing harness
*/

// ---------------- Payment history ----------------

typedef struct PaymentRecord {
    int from;
    int to;
    double amount;
    char note[128];
} PaymentRecord;

typedef struct PaymentHistory {
    PaymentRecord *items;
    int count;
    int capacity;
} PaymentHistory;

PaymentHistory *ph_create() {
    PaymentHistory *ph = malloc(sizeof(PaymentHistory));
    ph->capacity = 64; ph->count=0;
    ph->items = malloc(sizeof(PaymentRecord)*ph->capacity);
    return ph;
}

void ph_add(PaymentHistory *ph, int from, int to, double amount, const char *note) {
    if (ph->count >= ph->capacity) {
        ph->capacity *= 2; ph->items = realloc(ph->items, sizeof(PaymentRecord)*ph->capacity);
    }
    ph->items[ph->count].from = from;
    ph->items[ph->count].to = to;
    ph->items[ph->count].amount = amount;
    strncpy(ph->items[ph->count].note, note, sizeof(ph->items[ph->count].note)-1);
    ph->items[ph->count].note[sizeof(ph->items[ph->count].note)-1]='\0';
    ph->count++;
}

void ph_free(PaymentHistory *ph) { if (ph) { free(ph->items); free(ph); } }

// ---------------- UI helpers ----------------

void print_users(UserRegistry *ur) {
    printf("\nUsers (%d):\n", ur->count);
    for (int i=0;i<ur->count;i++) printf("  %d : %s\n", ur->users[i].id, ur->users[i].name);
}

void show_balances(BalanceMap *bm, UserRegistry *ur) {
    printf("\nNet balances:\n");
    for (int i=0;i<ur->count;i++) {
        double v = bm_get(bm, ur->users[i].id);
        printf("  %s : %+.2f\n", ur->users[i].name, v);
    }
}

// A simple function to apply settlement and populate payment history
void perform_settlement_and_record(BalanceMap *bm, UserRegistry *ur, PaymentHistory *ph) {
    DebtList *dl = settle_greedy(bm, ur);
    for (int i=0;i<dl->count;i++) {
        DebtEdge *e = &dl->edges[i];
        char note[128]; sprintf(note, "Auto-settlement");
        ph_add(ph, e->from, e->to, e->amount, note);
    }
    print_settlement(dl, ur);
    dl_free(dl);
}

// ---------------- Sample dataset helper ----------------

void populate_sample(UserRegistry *ur, TransactionList *tl) {
    // Simple scenario with 4 friends: Alice, Bob, Carol, Dave
    char *p1[] = {"Alice","Bob","Carol","Dave"};
    Transaction t1 = make_transaction(ur, "Alice", p1, 4, 1200.0, "Rent for April"); tl_add(tl, t1);
    char *p2[] = {"Alice","Bob","Carol"};
    Transaction t2 = make_transaction(ur, "Bob", p2, 3, 450.0, "Groceries"); tl_add(tl, t2);
    char *p3[] = {"Carol","Dave"};
    Transaction t3 = make_transaction(ur, "Carol", p3, 2, 800.0, "AC repair"); tl_add(tl, t3);
    char *p4[] = {"Bob","Dave"};
    Transaction t4 = make_transaction(ur, "Dave", p4, 2, 200.0, "Electricity top-up"); tl_add(tl, t4);
}

// ---------------- Utility: free all transactions participants pointers ----------------
void free_all_transactions(TransactionList *tl) {
    for (int i=0;i<tl->count;i++) transaction_free(&tl->items[i]);
}

// ---------------- Pretty printing of transaction log ----------------
void print_transactions(TransactionList *tl, UserRegistry *ur) {
    printf("\nTransactions:\n");
    for (int i=0;i<tl->count;i++) {
        Transaction *t = &tl->items[i];
        printf("  [%d] %s paid Rs %.2f for ", i, ur_name(ur, t->payer_id), t->amount);
        for (int j=0;j<t->part_count;j++) {
            printf("%s", ur_name(ur, t->participants[j]));
            if (j<t->part_count-1) printf(", ");
        }
        printf("  (note: %s)\n", t->note);
    }
}

// ---------------- Menu and interaction ----------------

void add_transaction_interactive(UserRegistry *ur, TransactionList *tl) {
    char payer[64]; char parts_line[256]; double amount; char note[128];
    printf("Enter payer name: "); if (!fgets(payer, sizeof(payer), stdin)) return; payer[strcspn(payer,"\n")]=0;
    if (strlen(payer)==0) { printf("No payer entered.\n"); return; }
    printf("Enter amount: "); if (scanf("%lf", &amount)!=1) { while(getchar()!= '\n'); printf("Invalid amount.\n"); return; }
    while(getchar()!='\n');
    printf("Enter participants as comma separated names (include payer if consumed): ");
    if (!fgets(parts_line, sizeof(parts_line), stdin)) return; parts_line[strcspn(parts_line,"\n")]=0;
    printf("Enter short note: "); if (!fgets(note, sizeof(note), stdin)) return; note[strcspn(note,"\n")]=0;
    // parse participants
    char *tokens[64]; int pcount=0;
    char *tk = strtok(parts_line, ",");
    while (tk && pcount<64) {
        while(isspace((unsigned char)*tk)) tk++;
        tokens[pcount++] = tk;
        tk = strtok(NULL, ",");
    }
    if (pcount==0) {
        printf("No participants entered.\n"); return;
    }
    Transaction t = make_transaction(ur, payer, tokens, pcount, amount, note);
    tl_add(tl, t);
    printf("Transaction added.\n");
}

void menu_loop() {
    UserRegistry *ur = ur_create();
    TransactionList *tl = tl_create();
    BalanceMap *bm = bm_create();
    PaymentHistory *ph = ph_create();

    // seed sample data
    populate_sample(ur, tl);

    int choice = 0;
    while (1) {
        printf("\n=== Cash Flow Optimizer Menu ===\n");
        printf("1. Show users and transactions\n");
        printf("2. Add transaction (interactive)\n");
        printf("3. Compute net balances\n");
        printf("4. Show suggested settlements\n");
        printf("5. Save transactions to file\n");
        printf("6. Load transactions from file (clears current)\n");
        printf("7. Show payment history\n");
        printf("8. Exit\n");
        printf("Choose: ");
        if (scanf("%d", &choice)!=1) { while(getchar()!='\n'); printf("Invalid input.\n"); continue; }
        while(getchar()!='\n');
        if (choice==1) { print_users(ur); print_transactions(tl, ur); }
        else if (choice==2) add_transaction_interactive(ur, tl);
        else if (choice==3) {
            // recompute balances from scratch
            // reset map
            bm_free(bm); bm = bm_create();
            compute_net_balances(tl, ur, bm);
            show_balances(bm, ur);
        }
        else if (choice==4) {
            bm_free(bm); bm = bm_create();
            compute_net_balances(tl, ur, bm);
            perform_settlement_and_record(bm, ur, ph);
        }
        else if (choice==5) {
            char fname[128]; printf("Enter filename to save: "); if (!fgets(fname, sizeof(fname), stdin)) continue; fname[strcspn(fname,"\n")]=0;
            if (save_transactions(fname, tl, ur)) printf("Saved to %s\n", fname); else printf("Failed to save.\n");
        }
        else if (choice==6) {
            char fname[128]; printf("Enter filename to load: "); if (!fgets(fname, sizeof(fname), stdin)) continue; fname[strcspn(fname,"\n")]=0;
            // clear current data
            for (int i=0;i<tl->count;i++) transaction_free(&tl->items[i]); tl->count=0;
            ur_free(ur); ur = ur_create();
            if (load_transactions(fname, tl, ur)) printf("Loaded from %s\n", fname); else printf("Failed to load.\n");
        }
        else if (choice==7) {
            printf("\nPayment history (%d):\n", ph->count);
            for (int i=0;i<ph->count;i++) {
                PaymentRecord *r = &ph->items[i];
                printf("  %s -> %s : Rs %.2f  (%s)\n", ur_name(ur, r->from), ur_name(ur, r->to), r->amount, r->note);
            }
        }
        else if (choice==8) {
            printf("Exiting...\n");
            break;
        }
        else printf("Unknown menu choice.\n");
    }

    // cleanup
    free_all_transactions(tl);
    tl_free(tl);
    ur_free(ur);
    bm_free(bm);
    ph_free(ph);
}

int main(int argc, char **argv) {
    printf("Cash Flow Optimizer - Simple Single-file C Project\n");
    printf("Team split: A - Data & I/O, B - Algorithm & Graph, C - UI & Reports\n");
    menu_loop();
    return 0;
}

/* ----------------------------- Presentation (slide draft) -----------------------------

Slide 1 - Title
  - Cash Flow Optimizer
  - Team: A, B, C
  - Short tagline: "Minimize payments to settle group debts"

Slide 2 - Problem Statement (A)
  - When groups share expenses, balancing payments is tedious
  - Goal: compute net balances and produce minimal set of transactions
  - Example: roommates splitting rent, utilities, grocery

Slide 3 - Data Model & I/O (A)
  - Users, Transactions
  - File format: payer|amount|participants|note
  - Hash map for net balances (why hash map?)

Slide 4 - Graph & Algorithm (B)
  - Nodes = users, directed edges = debt
  - Greedy approach: pair largest debtor with largest creditor
  - Complexity analysis: sorting O(n log n), settlement O(n)

Slide 5 - Implementation Details (B)
  - DebtList, BalanceMap structures
  - Settlement steps with example run

Slide 6 - UI & Reports (C)
  - Menu-driven interface
  - Payment history
  - Save/load transactions for persistence

Slide 7 - Demo plan (split by person)
  - A: Show file format and add few transactions
  - B: Run compute & explain algorithm with the sample
  - C: Show settlements and save/load + history

Slide 8 - Extensions & Future Work
  - Web/mobile UI, PDF export, reminders, multi-currency
  - Optimization: cycle cancellation improvements (advanced)

Slide 9 - Contributions & Responsibilities
  - Person A: Data structures, file I/O, user management
  - Person B: Graph & algorithm, settlement engine
  - Person C: UI, reporting, demo flow

Slide 10 - Q&A

End of slides.

*/
