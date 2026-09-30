import db from "./db.js";                   // -> db.js
export const List = () => <ul>{db.query().map(r => <li>{r}</li>)}</ul>;
