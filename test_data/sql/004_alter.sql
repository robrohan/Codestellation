-- -> 001; audit_log is created nowhere, so it stays unresolved
ALTER TABLE customers ADD COLUMN email TEXT;
INSERT INTO audit_log (msg) VALUES ('added email');
