KFC Management System

A complete KFC counter management experience, built entirely in C and operated through the command line.

A practical, terminal-based restaurant management system that brings together order processing, meal combos, discounts, inventory tracking, payments, and sales reporting in one application. No graphical interface, external dependencies, or unnecessary complexity — just C, a terminal, and a straightforward user experience.

Features
Order Management: Handle dine-in and takeaway orders with a live shopping cart.
Combo Meals: Create meal combinations with burgers, fries, and drinks.
Automatic Discounts: Apply predefined discounts when eligible product combinations are ordered.
Promo Codes: Support discount codes with validation and expiration handling.
Inventory Management: Track stock levels and display low-stock and sold-out alerts.
Payment Processing: Support cash payments and simulated card transactions.
Cart Control: Add, remove, and update items or cancel orders before payment.
Sales & Reports: View sales summaries, available coupons, and order records.
Persistent Storage: Preserve menu information, stock levels, and sales data between sessions.
Build and Run
bash
gcc -Wall -Wextra -std=c99 -o kfc kfc_management.c
./kfc

Quick Reference

Card payment simulation

Cards ending in 0000 are declined.
Other card numbers are approved.

Discount codes

KFC10 — 10% discount.
WELCOME20 — 20% discount.
Codes are case-insensitive.

Automatic combo discounts

Burger + Fries + Pepsi — 20%.
Burger + Fries — 10%.
Chicken + Coleslaw + Pepsi — 15%.
Hot Wings + Coleslaw — 10%.
Data Files
File	Purpose
kfc_data.txt	Stores menu data, inventory, and sales totals.
kfc_orders.txt	Records each successfully paid order.
Technical Details
Written in standard C99.
Uses the standard C library only.
Single-source-file implementation.
Stores monetary values as integer cents to avoid floating-point rounding errors.
Designed for compilation with strict warning flags, including -Wall, -Wextra, and -pedantic.

