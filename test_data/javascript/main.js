const db = require('./db');                 // -> db.js
const pg = require('pg');                   // package: unresolved
import('./lazy.mjs').then(m => m.default);  // -> lazy.mjs
module.exports = { db, pg };
