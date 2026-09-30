-- -> 002 (billing.invoices) and 001 (customers); i and c are aliases, not tables
CREATE VIEW open_invoices AS
  SELECT i.*, c.name
  FROM billing.invoices i
  JOIN customers c ON c.id = i.customer_id;
