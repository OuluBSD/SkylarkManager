# AGENTS - CompanySiteTests

Headless self-test for the composed Company site (`uppsrc/Company`). It boots one
engine site on a temporary root, exercises the routes over HTTP and the broker and
workflow directly, and removes the temporary root at exit. Run with
`bin/CompanySiteTests`. It never touches `./mgr` or personal data.
