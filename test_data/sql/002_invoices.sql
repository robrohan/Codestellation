-- foreign key -> 001_customers.sql
CREATE TABLE billing.invoices (
  id SERIAL PRIMARY KEY,
  customer_id INT REFERENCES customers(id),
  total NUMERIC
);
