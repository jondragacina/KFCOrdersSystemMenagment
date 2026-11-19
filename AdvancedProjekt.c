/*
 * kfc_management.c
 * KFC Management System (console application) - advanced level
 *
 * Customer flow:
 *   1. Welcome, then choose dine-in or take-away.
 *   2. "Do you want to add something?" Yes: choose a product or a combo meal
 *      and a quantity. The cart total is shown before and after every choice.
 *      The question comes back after every item.
 *   3. Review the cart. Change quantities, remove lines, add more, or cancel.
 *   4. Optional discount code. A code that is not available can be retried,
 *      or the order continues at the original price.
 *   5. Payment: cash, or card (simulated). A refused card can be tried again,
 *      paid in cash, or the order can be cancelled. The cart does not change.
 *   6. A coupon is shown on the screen.
 *
 * Combinations:
 *   - Combo meals are a burger and fries plus a drink that the customer
 *     chooses from the drinks in stock. The combo has its own fixed price.
 *   - Set discounts: when the cart holds a set of 2 or 3 single products
 *     (for example burger + fries + Pepsi), that set gets a percentage off.
 *     Combo meals are not part of set discounts.
 *
 * Stock:
 *   - Every product has a stock count. The customer is told when a product is
 *     sold out or when more pieces are asked for than are left.
 *   - Stock is taken only after a successful payment. A combo meal takes its
 *     burger, fries and drink from stock.
 *
 * Saving:
 *   - kfc_data.txt keeps the menu, the stock and the sales totals between runs.
 *   - kfc_orders.txt keeps one line for every paid order.
 *
 * Prices are stored in cents (whole numbers), so there are no rounding errors.
 *
 * Build:  gcc -Wall -Wextra -std=c99 -o kfc kfc_management.c
 * Run:    ./kfc
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------------------------------------------------------------- */
/* Constants                                                         */
/* ---------------------------------------------------------------- */

#define MAX_MENU_ITEMS      40          /* products and combo meals on the menu */
#define MAX_ORDER_LINES     20          /* different lines in one cart          */
#define MAX_QUANTITY        50          /* pieces of one line in a cart         */
#define MAX_NAME_LENGTH     40          /* name length, including the '\0'      */
#define MAX_PRICE           100000      /* highest price: $1000.00 (in cents)   */
#define MAX_PAYMENT         100000000   /* highest amount given (in cents)      */
#define MAX_STOCK_ADDITION  1000        /* most pieces added at one time        */
#define MAX_DISCOUNT_RULES  10          /* room for the set discount rules      */
#define MAX_INPUT_LENGTH    100         /* buffer for one line of input         */
#define LOW_STOCK_LEVEL     5           /* the report warns at this level       */
#define ORDER_LOG_FILE      "kfc_orders.txt"
#define DATA_FILE           "kfc_data.txt"

/* ---------------------------------------------------------------- */
/* Data types                                                        */
/* ---------------------------------------------------------------- */

typedef enum {
    BURGERS,
    CHICKEN_PIECES,
    SIDES,
    DRINKS,
    COMBO_MEALS,        /* after this come only the product categories */
    CATEGORY_COUNT
} Category;

typedef enum {
    DINE_IN = 1,
    TAKE_AWAY = 2
} OrderType;

typedef enum {
    PAY_CASH = 1,
    PAY_CARD = 2
} PaymentMethod;

/* One product or combo meal on the menu */
typedef struct {
    char name[MAX_NAME_LENGTH];
    Category category;
    int price;          /* in cents; a combo meal price already includes the drink */
    int stock;          /* pieces in stock; combo meals use the stock of their parts */
    int isAvailable;    /* 1 = customers can order it, 0 = hidden */
    int unitsSold;      /* pieces sold since the shop started */
    int salesValue;     /* list price of everything sold, in cents */
    int partA;          /* combo meals only: menu index of the burger, else -1 */
    int partB;          /* combo meals only: menu index of the fries, else -1 */
} MenuItem;

/* One line of the cart. A combo line also stores the drink the customer chose. */
typedef struct {
    int menuIndex;
    int drinkIndex;     /* combo meals only: menu index of the drink, else -1 */
    int quantity;
} OrderLine;

/* The cart the customer is building */
typedef struct {
    OrderType type;
    OrderLine lines[MAX_ORDER_LINES];
    int lineCount;
    int codeIndex;      /* discount code in use, -1 if none */
} CustomerOrder;

/* A set of 2 or 3 single products that gets a percentage off */
typedef struct {
    const char *label;
    const char *products[3];
    int productCount;
    int percent;
} SetDiscountRule;

/* A discount code. An inactive code is treated as not available. */
typedef struct {
    const char *code;
    int percent;
    int isActive;
} DiscountCode;

/* The price breakdown of the cart, all in cents */
typedef struct {
    int subtotal;                           /* list prices */
    int setUses[MAX_DISCOUNT_RULES];        /* how many sets of each rule */
    int setAmount[MAX_DISCOUNT_RULES];      /* discount given by each rule */
    int setDiscount;                        /* all set discounts together */
    int codeDiscount;                       /* discount from the code */
    int total;                              /* what the customer pays */
} Pricing;

/* Totals for the sales report. These are saved between runs. */
typedef struct {
    int totalOrders;
    int dineInOrders;
    int takeAwayOrders;
    int cashPayments;
    int cardPayments;
    int totalRevenue;       /* money actually paid, in cents */
    int totalDiscounts;     /* money given back as discounts, in cents */
} SalesSummary;

/* All the data of the program, kept in one place */
typedef struct {
    MenuItem items[MAX_MENU_ITEMS];
    int itemCount;
    SalesSummary summary;
} KfcSystem;

static const char *categoryNames[CATEGORY_COUNT] = {
    "Burgers", "Chicken Pieces", "Sides", "Drinks", "Combo Meals"
};

/* Sets of single products that get a discount when they are all in the cart.
   The rules are checked in order, so the bigger set comes first. */
static const SetDiscountRule setDiscountRules[] = {
    { "Burger + Fries + Pepsi",
      { "Zinger Burger", "French Fries (Large)", "Pepsi (0.5 L)" }, 3, 20 },
    { "Burger + Fries",
      { "Zinger Burger", "French Fries (Large)", NULL }, 2, 10 },
    { "Chicken + Coleslaw + Pepsi",
      { "Chicken Bucket (8 pcs)", "Coleslaw", "Pepsi (0.5 L)" }, 3, 15 },
    { "Hot Wings + Coleslaw",
      { "Hot Wings (6 pcs)", "Coleslaw", NULL }, 2, 10 },
};
#define SET_RULE_COUNT ((int)(sizeof(setDiscountRules) / sizeof(setDiscountRules[0])))

/* Discount codes. The expired code is kept in the list, but it is refused. */
static const DiscountCode discountCodes[] = {
    { "KFC10",     10, 1 },
    { "WELCOME20", 20, 1 },
    { "OLD2024",   25, 0 },
};
#define CODE_COUNT ((int)(sizeof(discountCodes) / sizeof(discountCodes[0])))

/* ---------------------------------------------------------------- */
/* Input and formatting helpers                                      */
/* ---------------------------------------------------------------- */

/* Reads one line and removes the newline at the end.
   If the input has ended (for example Ctrl+D), the program closes. */
static void readLine(char *buffer, size_t size)
{
    int ch;

    /* Show the prompt before the program waits for the keyboard */
    fflush(stdout);

    if (fgets(buffer, (int)size, stdin) == NULL) {
        printf("\nInput closed. Goodbye!\n");
        exit(EXIT_SUCCESS);
    }

    if (strchr(buffer, '\n') != NULL) {
        buffer[strcspn(buffer, "\n")] = '\0';
    } else {
        /* The line was longer than the buffer, so skip the rest of it */
        while ((ch = getchar()) != '\n' && ch != EOF) {
            /* nothing to do */
        }
    }
}

/* Returns 1 if the text is empty or has only spaces */
static int isBlank(const char *text)
{
    while (*text != '\0') {
        if (!isspace((unsigned char)*text)) {
            return 0;
        }
        text++;
    }
    return 1;
}

/* Asks for a whole number in the range [minValue, maxValue].
   Keeps asking until the answer is valid. */
static int readInt(const char *prompt, int minValue, int maxValue)
{
    char line[MAX_INPUT_LENGTH];
    char *endPtr;
    long value;

    while (1) {
        printf("%s", prompt);
        readLine(line, sizeof(line));

        value = strtol(line, &endPtr, 10);

        /* Spaces after the number are fine, anything else is not */
        while (*endPtr != '\0' && isspace((unsigned char)*endPtr)) {
            endPtr++;
        }

        if (endPtr == line || *endPtr != '\0') {
            printf("Invalid input. Please enter a whole number.\n");
        } else if (value < minValue || value > maxValue) {
            printf("Invalid number. Please enter a number from %d to %d.\n",
                   minValue, maxValue);
        } else {
            return (int)value;
        }
    }
}

/* Asks for a line of text. Empty input is not accepted. */
static void readText(const char *prompt, char *buffer, size_t size)
{
    while (1) {
        printf("%s", prompt);
        readLine(buffer, size);

        if (!isBlank(buffer)) {
            return;
        }
        printf("This cannot be empty.\n");
    }
}

/* Writes an amount in cents as text, for example 650 becomes "$6.50" */
static void formatMoney(int cents, char *buffer, size_t size)
{
    snprintf(buffer, size, "$%d.%02d", cents / 100, cents % 100);
}

/* Asks for an amount of money such as 6.50. A comma also works (6,50).
   Returns the amount in cents. */
static int readMoney(const char *prompt, int minCents, int maxCents)
{
    char line[MAX_INPUT_LENGTH];
    char *endPtr;
    char minText[32];
    char maxText[32];
    double value;
    size_t i;

    while (1) {
        printf("%s", prompt);
        readLine(line, sizeof(line));

        /* Albanian style decimal comma: turn it into a dot */
        for (i = 0; line[i] != '\0'; i++) {
            if (line[i] == ',') {
                line[i] = '.';
            }
        }

        value = strtod(line, &endPtr);
        while (*endPtr != '\0' && isspace((unsigned char)*endPtr)) {
            endPtr++;
        }

        if (endPtr == line || *endPtr != '\0') {
            printf("Invalid amount. Please enter a number such as 4.50.\n");
        } else if (!(value >= minCents / 100.0 && value <= maxCents / 100.0)) {
            /* Written this way, NaN is also rejected */
            formatMoney(minCents, minText, sizeof(minText));
            formatMoney(maxCents, maxText, sizeof(maxText));
            printf("Invalid amount. Please enter an amount from %s to %s.\n",
                   minText, maxText);
        } else {
            return (int)(value * 100.0 + 0.5);
        }
    }
}

/* Shows a yes/no question. Returns 1 for yes and 0 for no. */
static int askYesNo(const char *question)
{
    printf("\n%s\n", question);
    printf("1. Yes\n");
    printf("2. No\n");
    return readInt("Your choice: ", 1, 2) == 1;
}

/* Compares two texts without caring about upper and lower case */
static int sameText(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

/* ---------------------------------------------------------------- */
/* Menu setup                                                        */
/* ---------------------------------------------------------------- */

/* Returns the menu index of a product or combo with this name, or -1 */
static int findMenuItem(const KfcSystem *kfc, const char *name)
{
    int i;

    for (i = 0; i < kfc->itemCount; i++) {
        if (strcmp(kfc->items[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Adds a product to the end of the menu. Returns its index, or -1 if the menu is full. */
static int addMenuItem(KfcSystem *kfc, const char *name, Category category,
                       int price, int stock)
{
    MenuItem *item;

    if (kfc->itemCount >= MAX_MENU_ITEMS) {
        return -1;
    }

    item = &kfc->items[kfc->itemCount];
    snprintf(item->name, sizeof(item->name), "%s", name);
    item->category = category;
    item->price = price;
    item->stock = stock;
    item->isAvailable = 1;
    item->unitsSold = 0;
    item->salesValue = 0;
    item->partA = -1;
    item->partB = -1;
    kfc->itemCount++;
    return kfc->itemCount - 1;
}

/* Adds a combo meal: a burger and fries from the menu, plus a drink that the
   customer picks when the combo is ordered. */
static void addComboMeal(KfcSystem *kfc, const char *name, const char *burgerName,
                         const char *friesName, int price)
{
    int index = addMenuItem(kfc, name, COMBO_MEALS, price, 0);

    if (index >= 0) {
        kfc->items[index].partA = findMenuItem(kfc, burgerName);
        kfc->items[index].partB = findMenuItem(kfc, friesName);
    }
}

/* The starting menu. Prices are sample values in dollars. */
static void loadDefaultMenu(KfcSystem *kfc)
{
    addMenuItem(kfc, "Zinger Burger", BURGERS, 450, 20);
    addMenuItem(kfc, "Double Zinger Burger", BURGERS, 680, 10);
    addMenuItem(kfc, "Chicken Bucket (8 pcs)", CHICKEN_PIECES, 1490, 8);
    addMenuItem(kfc, "Hot Wings (6 pcs)", CHICKEN_PIECES, 520, 15);
    addMenuItem(kfc, "Popcorn Chicken", CHICKEN_PIECES, 350, 15);
    addMenuItem(kfc, "French Fries (Large)", SIDES, 200, 30);
    addMenuItem(kfc, "Coleslaw", SIDES, 180, 12);
    addMenuItem(kfc, "Pepsi (0.5 L)", DRINKS, 150, 25);
    addMenuItem(kfc, "Water (0.5 L)", DRINKS, 100, 20);

    /* Combo meals are added last, because they refer to the products above */
    addComboMeal(kfc, "Zinger Combo", "Zinger Burger", "French Fries (Large)", 590);
    addComboMeal(kfc, "Double Zinger Combo", "Double Zinger Burger",
                 "French Fries (Large)", 950);
}

/* Returns how many items customers can order right now */
static int countAvailableItems(const KfcSystem *kfc)
{
    int i;
    int count = 0;

    for (i = 0; i < kfc->itemCount; i++) {
        if (kfc->items[i].isAvailable) {
            count++;
        }
    }
    return count;
}

/* Asks the manager to choose one of the product categories (not combos) */
static Category chooseCategory(void)
{
    int i;

    printf("\nCategories:\n");
    for (i = 0; i < COMBO_MEALS; i++) {
        printf("%d. %s\n", i + 1, categoryNames[i]);
    }
    return (Category)(readInt("Category number: ", 1, COMBO_MEALS) - 1);
}

/* Shows the full menu with prices and stock (manager view) */
static void printFullMenu(const KfcSystem *kfc)
{
    char money[32];
    int i;

    printf("\n%-4s %-30s %-16s %10s %6s   %s\n",
           "No.", "Product", "Category", "Price", "Stock", "Status");
    for (i = 0; i < kfc->itemCount; i++) {
        const MenuItem *item = &kfc->items[i];
        const char *status = item->isAvailable ? "Available" : "Hidden";

        formatMoney(item->price, money, sizeof(money));
        if (item->category == COMBO_MEALS) {
            printf("%-4d %-30s %-16s %10s %6s   %s\n", i + 1, item->name,
                   categoryNames[item->category], money, "-", status);
        } else {
            printf("%-4d %-30s %-16s %10s %6d   %s\n", i + 1, item->name,
                   categoryNames[item->category], money, item->stock, status);
        }
    }
}

/* ---------------------------------------------------------------- */
/* Stock                                                             */
/* ---------------------------------------------------------------- */

/* How many pieces of one single product the cart already uses. This counts
   the single product lines and the pieces inside combo meals. */
static int piecesInCart(const KfcSystem *kfc, const CustomerOrder *order, int productIndex)
{
    int i;
    int used = 0;

    for (i = 0; i < order->lineCount; i++) {
        const OrderLine *line = &order->lines[i];
        const MenuItem *item = &kfc->items[line->menuIndex];

        if (line->menuIndex == productIndex) {
            used += line->quantity;
        } else if (item->category == COMBO_MEALS &&
                   (item->partA == productIndex || item->partB == productIndex ||
                    line->drinkIndex == productIndex)) {
            used += line->quantity;
        }
    }
    return used;
}

/* How many more pieces of one single product the customer can still add */
static int piecesLeft(const KfcSystem *kfc, const CustomerOrder *order, int productIndex)
{
    if (productIndex < 0) {
        return 0;
    }
    return kfc->items[productIndex].stock - piecesInCart(kfc, order, productIndex);
}

/* How many more of a product or combo meal the cart can take. A combo meal is
   limited by its burger, its fries and its drink. */
static int maxCanAdd(const KfcSystem *kfc, const CustomerOrder *order,
                     int menuIndex, int drinkIndex)
{
    const MenuItem *item = &kfc->items[menuIndex];
    int left;
    int other;

    if (item->category != COMBO_MEALS) {
        return piecesLeft(kfc, order, menuIndex);
    }

    left = piecesLeft(kfc, order, item->partA);
    other = piecesLeft(kfc, order, item->partB);
    if (other < left) {
        left = other;
    }
    other = piecesLeft(kfc, order, drinkIndex);
    if (other < left) {
        left = other;
    }
    return left;
}

/* ---------------------------------------------------------------- */
/* Pricing                                                           */
/* ---------------------------------------------------------------- */

/* Finds an active discount code. Returns its index, or -1 if it is not available. */
static int findDiscountCode(const char *text)
{
    int i;

    for (i = 0; i < CODE_COUNT; i++) {
        if (discountCodes[i].isActive && sameText(text, discountCodes[i].code)) {
            return i;
        }
    }
    return -1;
}

/* Works out the price of the cart. Set discounts use single products only.
   The discount code, if one is in use, is taken off the total after them. */
static void calculatePricing(const KfcSystem *kfc, const CustomerOrder *order,
                             Pricing *pricing)
{
    int remaining[MAX_MENU_ITEMS];
    int indexes[3];
    int i;
    int r;
    int k;
    int sets;
    int setValue;
    int ok;

    memset(pricing, 0, sizeof(*pricing));

    /* List prices of everything in the cart */
    for (i = 0; i < order->lineCount; i++) {
        const MenuItem *item = &kfc->items[order->lines[i].menuIndex];
        pricing->subtotal += item->price * order->lines[i].quantity;
    }

    /* Pieces of single products that can still be used in a set */
    for (i = 0; i < kfc->itemCount; i++) {
        remaining[i] = 0;
    }
    for (i = 0; i < order->lineCount; i++) {
        const OrderLine *line = &order->lines[i];

        if (kfc->items[line->menuIndex].category != COMBO_MEALS) {
            remaining[line->menuIndex] += line->quantity;
        }
    }

    /* Each rule takes as many complete sets as the cart allows */
    for (r = 0; r < SET_RULE_COUNT; r++) {
        const SetDiscountRule *rule = &setDiscountRules[r];

        ok = 1;
        for (k = 0; k < rule->productCount; k++) {
            indexes[k] = findMenuItem(kfc, rule->products[k]);
            if (indexes[k] < 0) {
                ok = 0;
            }
        }
        if (!ok) {
            continue;
        }

        sets = remaining[indexes[0]];
        for (k = 1; k < rule->productCount; k++) {
            if (remaining[indexes[k]] < sets) {
                sets = remaining[indexes[k]];
            }
        }
        if (sets <= 0) {
            continue;
        }

        setValue = 0;
        for (k = 0; k < rule->productCount; k++) {
            setValue += kfc->items[indexes[k]].price;
            remaining[indexes[k]] -= sets;
        }

        pricing->setUses[r] = sets;
        pricing->setAmount[r] = setValue * sets * rule->percent / 100;
        pricing->setDiscount += pricing->setAmount[r];
    }

    pricing->total = pricing->subtotal - pricing->setDiscount;

    if (order->codeIndex >= 0) {
        pricing->codeDiscount = pricing->total * discountCodes[order->codeIndex].percent / 100;
        pricing->total -= pricing->codeDiscount;
    }
}

/* ---------------------------------------------------------------- */
/* Customer cart                                                     */
/* ---------------------------------------------------------------- */

/* Shows the cart total so far, so the customer sees the cash value at each step */
static void printCartTotal(const KfcSystem *kfc, const CustomerOrder *order)
{
    Pricing pricing;
    char money[32];

    calculatePricing(kfc, order, &pricing);
    formatMoney(pricing.total, money, sizeof(money));
    printf("Cart total so far: %s\n", money);
}

/* Prints the menu for customers, grouped by category, with the stock left */
static void showMenuForCustomers(const KfcSystem *kfc, const CustomerOrder *order)
{
    Category category;
    int i;
    int left;
    int headerShown;
    char money[32];

    printf("\n------------------ MENU ------------------\n");
    printCartTotal(kfc, order);

    for (category = BURGERS; category < CATEGORY_COUNT; category++) {
        headerShown = 0;

        for (i = 0; i < kfc->itemCount; i++) {
            const MenuItem *item = &kfc->items[i];

            if (item->category != category || !item->isAvailable) {
                continue;
            }
            if (!headerShown) {
                printf("\n%s\n", categoryNames[category]);
                headerShown = 1;
            }

            formatMoney(item->price, money, sizeof(money));
            printf("%3d. %-30s %10s", i + 1, item->name, money);

            if (category == COMBO_MEALS) {
                printf("   (drink of your choice)");
            } else {
                left = piecesLeft(kfc, order, i);
                if (left > 0) {
                    printf("   (%d left)", left);
                } else {
                    printf("   SOLD OUT");
                }
            }
            printf("\n");
        }
    }
    printf("\n------------------------------------------\n");
}

/* Asks the customer to eat in or take the order away */
static OrderType chooseOrderType(void)
{
    printf("\nHow would you like your order?\n");
    printf("1. Dine-in (eat in the restaurant)\n");
    printf("2. Take-away\n");
    return (OrderType)readInt("Your choice: ", 1, 2);
}

/* Adds a line to the cart. If the same product (and the same drink, for a
   combo) is already there, only its quantity goes up. Returns 1 on success. */
static int addToOrder(CustomerOrder *order, int menuIndex, int drinkIndex, int quantity)
{
    int i;

    for (i = 0; i < order->lineCount; i++) {
        OrderLine *line = &order->lines[i];

        if (line->menuIndex == menuIndex && line->drinkIndex == drinkIndex) {
            if (line->quantity + quantity > MAX_QUANTITY) {
                printf("You can order at most %d pieces of one item.\n", MAX_QUANTITY);
                return 0;
            }
            line->quantity += quantity;
            return 1;
        }
    }

    if (order->lineCount >= MAX_ORDER_LINES) {
        printf("A cart can have at most %d different lines.\n", MAX_ORDER_LINES);
        return 0;
    }

    order->lines[order->lineCount].menuIndex = menuIndex;
    order->lines[order->lineCount].drinkIndex = drinkIndex;
    order->lines[order->lineCount].quantity = quantity;
    order->lineCount++;
    return 1;
}

/* Lists the drinks that can go into a combo meal and asks the customer to pick one.
   Returns the drink's menu index, or -1 if no drink is left. */
static int chooseComboDrink(const KfcSystem *kfc, const CustomerOrder *order)
{
    int i;
    int number;
    int shown = 0;

    printf("\nChoose the drink for this combo (included in the price):\n");
    for (i = 0; i < kfc->itemCount; i++) {
        const MenuItem *item = &kfc->items[i];

        if (item->category == DRINKS && item->isAvailable && piecesLeft(kfc, order, i) > 0) {
            printf("%3d. %-30s %d left\n", i + 1, item->name, piecesLeft(kfc, order, i));
            shown++;
        }
    }

    if (shown == 0) {
        printf("Sorry, there are no drinks left for combo meals.\n");
        return -1;
    }

    while (1) {
        number = readInt("Drink number: ", 1, kfc->itemCount);
        i = number - 1;

        if (kfc->items[i].category == DRINKS && kfc->items[i].isAvailable &&
            piecesLeft(kfc, order, i) > 0) {
            return i;
        }
        printf("Please choose one of the drinks listed above.\n");
    }
}

/* Shows the menu and asks for a product or combo meal and a quantity, then adds it
   to the cart. Returns 0 if the customer typed 0 to go back, otherwise 1. */
static int addOneProduct(KfcSystem *kfc, CustomerOrder *order)
{
    const MenuItem *item = NULL;
    int number;
    int index = 0;
    int drinkIndex = -1;
    int quantity;
    int left;
    char money[32];

    showMenuForCustomers(kfc, order);

    /* Pick an item that is on sale and not sold out */
    while (1) {
        number = readInt("Product number (0 = back): ", 0, kfc->itemCount);
        if (number == 0) {
            return 0;
        }

        index = number - 1;
        item = &kfc->items[index];

        if (!item->isAvailable) {
            printf("Sorry, that item is not available right now.\n");
        } else if (item->category == COMBO_MEALS) {
            if (piecesLeft(kfc, order, item->partA) <= 0 ||
                piecesLeft(kfc, order, item->partB) <= 0) {
                printf("Sorry, %s is sold out.\n", item->name);
            } else {
                break;
            }
        } else if (piecesLeft(kfc, order, index) <= 0) {
            printf("Sorry, %s is sold out.\n", item->name);
        } else {
            break;
        }
    }

    /* A combo meal shows its price, then the customer picks the drink */
    if (item->category == COMBO_MEALS) {
        formatMoney(item->price, money, sizeof(money));
        printf("\n%s costs %s.\n", item->name, money);

        drinkIndex = chooseComboDrink(kfc, order);
        if (drinkIndex < 0) {
            return 1;
        }
    }

    left = maxCanAdd(kfc, order, index, drinkIndex);
    if (left <= 0) {
        printf("Sorry, %s is sold out.\n", item->name);
        return 1;
    }

    /* Tell the customer when more pieces are asked for than are in stock */
    while (1) {
        quantity = readInt("Quantity: ", 1, MAX_QUANTITY);
        if (quantity <= left) {
            break;
        }
        printf("Sorry, only %d piece(s) of %s are left in stock.\n", left, item->name);
    }

    if (addToOrder(order, index, drinkIndex, quantity)) {
        printf("Added to your order.\n");
        printCartTotal(kfc, order);
    }
    return 1;
}

/* Changes the quantity of one cart line, within the stock that is left */
static void changeLineQuantity(const KfcSystem *kfc, CustomerOrder *order)
{
    OrderLine *line;
    int lineNumber;
    int oldQuantity;
    int newQuantity;
    int left;

    lineNumber = readInt("Line number to change: ", 1, order->lineCount);
    line = &order->lines[lineNumber - 1];

    /* Free this line's pieces first, so the limit is what this line could take */
    oldQuantity = line->quantity;
    line->quantity = 0;
    left = maxCanAdd(kfc, order, line->menuIndex, line->drinkIndex);
    line->quantity = oldQuantity;

    while (1) {
        newQuantity = readInt("New quantity: ", 1, MAX_QUANTITY);
        if (newQuantity <= left) {
            break;
        }
        printf("Sorry, only %d piece(s) are left in stock.\n", left);
    }

    line->quantity = newQuantity;
    printf("Quantity updated.\n");
}

/* Removes one line from the cart. The lines after it move up one place. */
static void removeLine(CustomerOrder *order)
{
    int lineNumber;
    int i;

    lineNumber = readInt("Line number to remove: ", 1, order->lineCount);
    for (i = lineNumber - 1; i < order->lineCount - 1; i++) {
        order->lines[i] = order->lines[i + 1];
    }
    order->lineCount--;
    printf("Product removed from your order.\n");
}

/* Shows the cart, the discounts and the total */
static void showOrder(const KfcSystem *kfc, const CustomerOrder *order)
{
    Pricing pricing;
    char money[32];
    int i;
    int r;

    calculatePricing(kfc, order, &pricing);

    printf("\n--------------------- YOUR ORDER ---------------------\n");
    for (i = 0; i < order->lineCount; i++) {
        const OrderLine *line = &order->lines[i];
        const MenuItem *item = &kfc->items[line->menuIndex];

        formatMoney(item->price * line->quantity, money, sizeof(money));
        printf("%2d. %3d x %-30s %10s\n", i + 1, line->quantity, item->name, money);
        if (line->drinkIndex >= 0) {
            printf("        with drink: %s\n", kfc->items[line->drinkIndex].name);
        }
    }
    printf("------------------------------------------------------\n");

    formatMoney(pricing.subtotal, money, sizeof(money));
    printf("Subtotal: %s\n", money);

    for (r = 0; r < SET_RULE_COUNT; r++) {
        if (pricing.setUses[r] > 0) {
            formatMoney(pricing.setAmount[r], money, sizeof(money));
            printf("Set discount %s (%d%%) x%d: -%s\n",
                   setDiscountRules[r].label, setDiscountRules[r].percent,
                   pricing.setUses[r], money);
        }
    }

    if (order->codeIndex >= 0) {
        formatMoney(pricing.codeDiscount, money, sizeof(money));
        printf("Discount code %s (%d%%): -%s\n",
               discountCodes[order->codeIndex].code,
               discountCodes[order->codeIndex].percent, money);
    }

    formatMoney(pricing.total, money, sizeof(money));
    printf("TOTAL: %s\n", money);
}

/* Lets the customer change the cart before payment. The cart and the total are
   shown again after every change. Returns 1 to continue to payment, or 0 if the
   whole order is cancelled. */
static int reviewCart(KfcSystem *kfc, CustomerOrder *order)
{
    int choice;

    while (1) {
        /* An empty cart goes back to the menu to choose products again */
        if (order->lineCount == 0) {
            printf("\nYour cart is empty. Please choose products from the menu.\n");
            if (!addOneProduct(kfc, order)) {
                return 0;   /* the customer left with an empty cart */
            }
            continue;
        }

        showOrder(kfc, order);
        printf("\n1. Continue to payment\n");
        printf("2. Add more products\n");
        printf("3. Change a quantity\n");
        printf("4. Remove a product\n");
        printf("5. Cancel the whole order\n");

        choice = readInt("Your choice: ", 1, 5);

        switch (choice) {
        case 1:
            return 1;
        case 2:
            addOneProduct(kfc, order);
            break;
        case 3:
            changeLineQuantity(kfc, order);
            break;
        case 4:
            removeLine(order);
            break;
        case 5:
            if (askYesNo("Are you sure you want to cancel the whole order?")) {
                return 0;
            }
            break;
        }
    }
}

/* The question "Do you want to add something?". It comes back after every item.
   Returns 0 if the customer says no with an empty cart. */
static int collectProducts(KfcSystem *kfc, CustomerOrder *order)
{
    while (askYesNo("Do you want to add something?")) {
        addOneProduct(kfc, order);
    }

    if (order->lineCount == 0) {
        printf("Your order is empty, so it has been cancelled.\n");
        return 0;
    }
    return 1;
}

/* Asks for a discount code before payment. A code that is not available can be
   retried, or the order continues at the original price. */
static void askDiscountCode(CustomerOrder *order)
{
    char code[MAX_INPUT_LENGTH];
    int index;
    int choice;

    order->codeIndex = -1;
    if (!askYesNo("Do you have a discount code?")) {
        return;
    }

    while (1) {
        readText("Discount code: ", code, sizeof(code));
        index = findDiscountCode(code);

        if (index >= 0) {
            order->codeIndex = index;
            printf("Code %s accepted: %d%% off the total.\n",
                   discountCodes[index].code, discountCodes[index].percent);
            return;
        }

        printf("This code is not available (wrong or expired).\n");
        printf("1. Try another code\n");
        printf("2. Continue at the original price\n");
        choice = readInt("Your choice: ", 1, 2);

        if (choice == 2) {
            printf("The order continues at the original price.\n");
            return;
        }
    }
}

/* ---------------------------------------------------------------- */
/* Payment and coupon                                                */
/* ---------------------------------------------------------------- */

/* Asks the customer to pay in cash or by card */
static PaymentMethod choosePaymentMethod(void)
{
    printf("\nHow would you like to pay?\n");
    printf("1. Cash\n");
    printf("2. Card\n");
    return (PaymentMethod)readInt("Your choice: ", 1, 2);
}

/* Takes the cash from the customer. If it is not enough, shows how much is
   still missing and asks again. Returns the amount given, in cents. */
static int takeCashPayment(int total)
{
    char money[32];
    int paid;

    while (1) {
        paid = readMoney("Amount given (for example 10.00): ", 1, MAX_PAYMENT);
        if (paid >= total) {
            return paid;
        }
        formatMoney(total - paid, money, sizeof(money));
        printf("The amount is not enough. Still missing: %s\n", money);
    }
}

/* Simulated card payment. The customer types the last 4 digits of the card.
   In this simulation the card ending in 0000 is always refused.
   Returns 1 if approved, 0 if refused. */
static int simulateCardPayment(int total)
{
    char money[32];
    int lastFour;

    formatMoney(total, money, sizeof(money));
    printf("\nCard payment of %s.\n", money);
    lastFour = readInt("Last 4 digits of the card (0000 is refused for testing): ",
                       0, 9999);
    printf("Contacting the bank ...\n");

    if (lastFour == 0) {
        return 0;
    }
    printf("Payment approved.\n");
    return 1;
}

/* Takes the payment. A refused card can be tried again, paid in cash, or the
   order can be cancelled. The cart is not changed by a refusal.
   Returns 1 when the order is paid, 0 when it is cancelled. */
static int payForOrder(const Pricing *pricing, PaymentMethod *method, int *paid)
{
    int choice;

    *method = choosePaymentMethod();
    if (*method == PAY_CASH) {
        *paid = takeCashPayment(pricing->total);
        return 1;
    }

    while (1) {
        if (simulateCardPayment(pricing->total)) {
            *paid = pricing->total;
            return 1;
        }

        printf("\nThe card payment was refused. Your cart has not changed.\n");
        printf("1. Try the card again\n");
        printf("2. Pay with cash instead\n");
        printf("3. Cancel the order\n");
        choice = readInt("Your choice: ", 1, 3);

        if (choice == 2) {
            *method = PAY_CASH;
            *paid = takeCashPayment(pricing->total);
            return 1;
        }
        if (choice == 3) {
            return 0;
        }
        /* choice 1: the loop asks for the card again */
    }
}

/* Shows the coupon on the screen after a successful payment */
static void printCoupon(const KfcSystem *kfc, const CustomerOrder *order,
                        int orderNumber, PaymentMethod method,
                        const Pricing *pricing, int paid)
{
    char money[32];
    int i;
    int r;

    printf("\n==================== COUPON ====================\n");
    printf("Order number:   #%d\n", orderNumber);
    printf("Order type:     %s\n", order->type == DINE_IN ? "Dine-in" : "Take-away");
    printf("------------------------------------------------\n");

    for (i = 0; i < order->lineCount; i++) {
        const OrderLine *line = &order->lines[i];
        const MenuItem *item = &kfc->items[line->menuIndex];

        formatMoney(item->price * line->quantity, money, sizeof(money));
        printf("%3d x %-30s %10s\n", line->quantity, item->name, money);
        if (line->drinkIndex >= 0) {
            printf("      with drink: %s\n", kfc->items[line->drinkIndex].name);
        }
    }

    printf("------------------------------------------------\n");
    formatMoney(pricing->subtotal, money, sizeof(money));
    printf("Subtotal:       %10s\n", money);

    for (r = 0; r < SET_RULE_COUNT; r++) {
        if (pricing->setUses[r] > 0) {
            formatMoney(pricing->setAmount[r], money, sizeof(money));
            printf("Set %s x%d: -%s\n", setDiscountRules[r].label,
                   pricing->setUses[r], money);
        }
    }

    if (order->codeIndex >= 0) {
        formatMoney(pricing->codeDiscount, money, sizeof(money));
        printf("Code %s: -%s\n", discountCodes[order->codeIndex].code, money);
    }

    formatMoney(pricing->total, money, sizeof(money));
    printf("Total:          %10s\n", money);
    printf("Payment method: %s\n", method == PAY_CASH ? "Cash" : "Card");

    if (method == PAY_CASH) {
        formatMoney(paid, money, sizeof(money));
        printf("Given:          %10s\n", money);
        formatMoney(paid - pricing->total, money, sizeof(money));
        printf("Change:         %10s\n", money);
    }

    printf("================================================\n");
    printf("Thank you for choosing KFC!\n");
}

/* Updates the totals, the sold counts and the stock after a paid order.
   Stock is taken here, and only here, so a refused or cancelled order keeps it. */
static void recordSale(KfcSystem *kfc, const CustomerOrder *order,
                       const Pricing *pricing, PaymentMethod method)
{
    SalesSummary *summary = &kfc->summary;
    int i;

    summary->totalOrders++;
    if (order->type == DINE_IN) {
        summary->dineInOrders++;
    } else {
        summary->takeAwayOrders++;
    }

    if (method == PAY_CASH) {
        summary->cashPayments++;
    } else {
        summary->cardPayments++;
    }
    summary->totalRevenue += pricing->total;
    summary->totalDiscounts += pricing->subtotal - pricing->total;

    for (i = 0; i < order->lineCount; i++) {
        const OrderLine *line = &order->lines[i];
        MenuItem *item = &kfc->items[line->menuIndex];

        item->unitsSold += line->quantity;
        item->salesValue += item->price * line->quantity;

        if (item->category == COMBO_MEALS) {
            /* One combo meal uses one burger, one fries and one drink */
            kfc->items[item->partA].stock -= line->quantity;
            kfc->items[item->partB].stock -= line->quantity;
            kfc->items[line->drinkIndex].stock -= line->quantity;
        } else {
            item->stock -= line->quantity;
        }
    }
}

/* Writes one line about the paid order to the log file.
   The file is opened in append mode, so older orders are kept. */
static void saveOrderToFile(const KfcSystem *kfc, const CustomerOrder *order,
                            int orderNumber, PaymentMethod method,
                            const Pricing *pricing, int paid)
{
    FILE *file;
    char timeText[32];
    char money[32];
    time_t now;
    int i;

    file = fopen(ORDER_LOG_FILE, "a");
    if (file == NULL) {
        printf("Warning: could not write to %s\n", ORDER_LOG_FILE);
        return;
    }

    now = time(NULL);
    strftime(timeText, sizeof(timeText), "%Y-%m-%d %H:%M", localtime(&now));

    fprintf(file, "%s | #%d | %s | ", timeText, orderNumber,
            order->type == DINE_IN ? "Dine-in" : "Take-away");

    for (i = 0; i < order->lineCount; i++) {
        const OrderLine *line = &order->lines[i];

        fprintf(file, "%dx %s", line->quantity, kfc->items[line->menuIndex].name);
        if (line->drinkIndex >= 0) {
            fprintf(file, " (drink: %s)", kfc->items[line->drinkIndex].name);
        }
        fprintf(file, "; ");
    }

    formatMoney(pricing->total, money, sizeof(money));
    fprintf(file, "| total %s", money);
    if (order->codeIndex >= 0) {
        fprintf(file, " | code %s", discountCodes[order->codeIndex].code);
    }
    fprintf(file, " | paid by %s", method == PAY_CASH ? "cash" : "card");

    if (method == PAY_CASH) {
        formatMoney(paid, money, sizeof(money));
        fprintf(file, " | given %s", money);
        formatMoney(paid - pricing->total, money, sizeof(money));
        fprintf(file, " | change %s", money);
    }
    fprintf(file, "\n");

    fclose(file);
}

/* Runs the whole customer flow, from dine-in or take-away until the coupon */
static void takeCustomerOrder(KfcSystem *kfc)
{
    CustomerOrder order;
    Pricing pricing;
    PaymentMethod method;
    int paid = 0;
    int orderNumber;

    if (countAvailableItems(kfc) == 0) {
        printf("Sorry, there are no products available right now.\n");
        return;
    }

    /* "Zgjedh ne lokal ose me vete": dine-in or take-away */
    order.type = chooseOrderType();
    order.lineCount = 0;
    order.codeIndex = -1;

    /* "Deshiron te shtosh dicka?" and the product loop */
    if (!collectProducts(kfc, &order)) {
        return;
    }

    /* "Shfaq produktet, sasite dhe totalin", plus changes before payment */
    if (!reviewCart(kfc, &order)) {
        printf("\nOrder cancelled. Nothing was charged and the stock was not changed.\n");
        return;
    }

    /* Discount code, then the final price before payment */
    askDiscountCode(&order);
    calculatePricing(kfc, &order, &pricing);
    showOrder(kfc, &order);

    if (!payForOrder(&pricing, &method, &paid)) {
        printf("\nOrder cancelled. Nothing was charged and the stock was not changed.\n");
        return;
    }

    /* "Konfirmo porosine": save the sale, take the stock and show the coupon */
    orderNumber = kfc->summary.totalOrders + 1;
    recordSale(kfc, &order, &pricing, method);
    saveOrderToFile(kfc, &order, orderNumber, method, &pricing, paid);
    printCoupon(kfc, &order, orderNumber, method, &pricing, paid);
}

/* ---------------------------------------------------------------- */
/* Manager functions                                                 */
/* ---------------------------------------------------------------- */

/* Adds a new product to the menu */
static void addNewProduct(KfcSystem *kfc)
{
    char name[MAX_NAME_LENGTH];
    char money[32];
    Category category;
    int price;
    int stock;

    if (kfc->itemCount >= MAX_MENU_ITEMS) {
        printf("The menu is full. You cannot add more products.\n");
        return;
    }

    readText("Product name: ", name, sizeof(name));
    if (findMenuItem(kfc, name) >= 0) {
        printf("A product with this name already exists.\n");
        return;
    }

    category = chooseCategory();
    price = readMoney("Price (for example 4.50): ", 1, MAX_PRICE);
    stock = readInt("Pieces in stock: ", 0, MAX_STOCK_ADDITION);

    addMenuItem(kfc, name, category, price, stock);
    formatMoney(price, money, sizeof(money));
    printf("\"%s\" was added to %s at %s with %d in stock.\n",
           name, categoryNames[category], money, stock);
}

/* Changes the price of one product. Only the next orders use the new price. */
static void changePrice(KfcSystem *kfc)
{
    MenuItem *item;
    char oldText[32];
    char newText[32];
    int number;
    int newPrice;

    printFullMenu(kfc);
    number = readInt("Product number: ", 1, kfc->itemCount);
    item = &kfc->items[number - 1];

    newPrice = readMoney("New price (for example 4.50): ", 1, MAX_PRICE);

    formatMoney(item->price, oldText, sizeof(oldText));
    formatMoney(newPrice, newText, sizeof(newText));
    printf("%s: %s -> %s\n", item->name, oldText, newText);
    item->price = newPrice;
}

/* Shows or hides a product from customers. Hidden products keep their sales. */
static void toggleAvailability(KfcSystem *kfc)
{
    MenuItem *item;
    int number;

    printFullMenu(kfc);
    number = readInt("Product number: ", 1, kfc->itemCount);
    item = &kfc->items[number - 1];

    item->isAvailable = !item->isAvailable;
    printf("%s is now %s.\n", item->name,
           item->isAvailable ? "available" : "hidden from customers");
}

/* Adds pieces to the stock of one product */
static void addStock(KfcSystem *kfc)
{
    MenuItem *item;
    int number;
    int pieces;

    printFullMenu(kfc);
    number = readInt("Product number: ", 1, kfc->itemCount);
    item = &kfc->items[number - 1];

    if (item->category == COMBO_MEALS) {
        printf("Combo meals use the stock of their burger, fries and drink.\n");
        printf("Add stock to those products instead.\n");
        return;
    }

    pieces = readInt("Pieces to add: ", 1, MAX_STOCK_ADDITION);
    item->stock += pieces;
    printf("%s now has %d pieces in stock.\n", item->name, item->stock);
}

/* The menu management screen */
static void manageMenu(KfcSystem *kfc)
{
    int choice;

    while (1) {
        printf("\n===== MANAGE MENU =====\n");
        printf("1. View menu\n");
        printf("2. Add a product\n");
        printf("3. Change a price\n");
        printf("4. Show / hide a product\n");
        printf("5. Add stock\n");
        printf("0. Back\n");

        choice = readInt("Your choice: ", 0, 5);

        switch (choice) {
        case 1:
            printFullMenu(kfc);
            break;
        case 2:
            addNewProduct(kfc);
            break;
        case 3:
            changePrice(kfc);
            break;
        case 4:
            toggleAvailability(kfc);
            break;
        case 5:
            addStock(kfc);
            break;
        case 0:
            return;
        }
    }
}

/* Shows the totals, the stock, the low stock warnings and the best seller */
static void showSalesReport(const KfcSystem *kfc)
{
    const SalesSummary *summary = &kfc->summary;
    const MenuItem *item;
    char money[32];
    int gross = 0;
    int bestIndex = -1;
    int lowFound = 0;
    int i;

    for (i = 0; i < kfc->itemCount; i++) {
        gross += kfc->items[i].salesValue;
    }

    printf("\n=============== SALES REPORT ===============\n");
    printf("(Saved between runs. Full order history: %s)\n", ORDER_LOG_FILE);
    printf("Total orders:        %d (dine-in %d, take-away %d)\n",
           summary->totalOrders, summary->dineInOrders, summary->takeAwayOrders);
    printf("Paid by cash:        %d\n", summary->cashPayments);
    printf("Paid by card:        %d\n", summary->cardPayments);

    formatMoney(gross, money, sizeof(money));
    printf("Sales at list price: %s\n", money);
    formatMoney(summary->totalDiscounts, money, sizeof(money));
    printf("Discounts given:     %s\n", money);
    formatMoney(summary->totalRevenue, money, sizeof(money));
    printf("Money received:      %s\n", money);

    if (summary->totalOrders > 0) {
        formatMoney(summary->totalRevenue / summary->totalOrders, money, sizeof(money));
        printf("Average per order:   %s\n", money);
    }

    printf("\n%-30s %6s %6s %10s\n", "Product", "Sold", "Stock", "Sales");
    for (i = 0; i < kfc->itemCount; i++) {
        item = &kfc->items[i];

        formatMoney(item->salesValue, money, sizeof(money));
        if (item->category == COMBO_MEALS) {
            printf("%-30s %6d %6s %10s\n", item->name, item->unitsSold, "-", money);
        } else {
            printf("%-30s %6d %6d %10s\n", item->name, item->unitsSold, item->stock, money);
        }

        if (item->unitsSold > 0 &&
            (bestIndex < 0 || item->unitsSold > kfc->items[bestIndex].unitsSold)) {
            bestIndex = i;
        }
    }

    printf("\nLow stock (%d or fewer pieces):\n", LOW_STOCK_LEVEL);
    for (i = 0; i < kfc->itemCount; i++) {
        item = &kfc->items[i];

        if (item->category != COMBO_MEALS && item->stock <= LOW_STOCK_LEVEL) {
            printf("  %s: %d left\n", item->name, item->stock);
            lowFound = 1;
        }
    }
    if (!lowFound) {
        printf("  None\n");
    }

    if (bestIndex >= 0) {
        printf("\nBest seller: %s (%d sold)\n",
               kfc->items[bestIndex].name, kfc->items[bestIndex].unitsSold);
    } else {
        printf("\nNo sales yet.\n");
    }
}

/* ---------------------------------------------------------------- */
/* Saving and loading                                                */
/* ---------------------------------------------------------------- */

/* Saves the menu, the stock and the sales totals, so they survive a restart */
static void saveData(const KfcSystem *kfc)
{
    const SalesSummary *s = &kfc->summary;
    FILE *file;
    int i;

    file = fopen(DATA_FILE, "w");
    if (file == NULL) {
        printf("Warning: could not save %s\n", DATA_FILE);
        return;
    }

    fprintf(file, "%d %d %d %d %d %d %d\n", s->totalOrders, s->dineInOrders,
            s->takeAwayOrders, s->cashPayments, s->cardPayments,
            s->totalRevenue, s->totalDiscounts);

    for (i = 0; i < kfc->itemCount; i++) {
        const MenuItem *item = &kfc->items[i];

        fprintf(file, "%s|%d|%d|%d|%d|%d|%d|%d|%d\n", item->name, (int)item->category,
                item->price, item->stock, item->isAvailable, item->unitsSold,
                item->salesValue, item->partA, item->partB);
    }

    fclose(file);
}

/* Loads the saved data. Returns 1 if the menu was loaded, 0 if there is no usable file. */
static int loadData(KfcSystem *kfc)
{
    SalesSummary *s = &kfc->summary;
    FILE *file;
    MenuItem item;
    int category;
    int count = 0;

    file = fopen(DATA_FILE, "r");
    if (file == NULL) {
        return 0;
    }

    if (fscanf(file, "%d %d %d %d %d %d %d", &s->totalOrders, &s->dineInOrders,
               &s->takeAwayOrders, &s->cashPayments, &s->cardPayments,
               &s->totalRevenue, &s->totalDiscounts) != 7) {
        fclose(file);
        memset(s, 0, sizeof(*s));
        return 0;
    }

    while (count < MAX_MENU_ITEMS &&
           fscanf(file, " %39[^|]|%d|%d|%d|%d|%d|%d|%d|%d", item.name, &category,
                  &item.price, &item.stock, &item.isAvailable, &item.unitsSold,
                  &item.salesValue, &item.partA, &item.partB) == 9) {
        item.category = (Category)category;
        kfc->items[count] = item;
        count++;
    }

    fclose(file);
    kfc->itemCount = count;

    if (count == 0) {
        memset(s, 0, sizeof(*s));
        return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------- */
/* Main screen                                                       */
/* ---------------------------------------------------------------- */

static void printMainMenu(void)
{
    printf("\n==========================================\n");
    printf("          KFC MANAGEMENT SYSTEM\n");
    printf("==========================================\n");
    printf("Welcome to KFC!\n\n");
    printf("1. New customer order\n");
    printf("2. Manage menu\n");
    printf("3. Sales report\n");
    printf("0. Exit\n");
}

int main(void)
{
    KfcSystem kfc;
    int choice;

    /* Start with empty data. Use the saved data if it exists, else the default menu. */
    memset(&kfc, 0, sizeof(kfc));
    if (!loadData(&kfc)) {
        loadDefaultMenu(&kfc);
    }

    while (1) {
        printMainMenu();
        choice = readInt("Your choice: ", 0, 3);

        switch (choice) {
        case 1:
            takeCustomerOrder(&kfc);
            saveData(&kfc);
            break;
        case 2:
            manageMenu(&kfc);
            saveData(&kfc);
            break;
        case 3:
            showSalesReport(&kfc);
            break;
        case 0:
            printf("Goodbye!\n");
            return EXIT_SUCCESS;
        }
    }
}