// The front door: send a logged-in person to their home screen, everyone else to the login screen.
import { loadMessages } from '../i18n.js';
import { api } from '../api.js';
import { HOME } from '../layout.js';

await loadMessages();
try {
  const me = await api.get('/auth/me');
  location.replace(me.must_change_password ? '/login.html?step=password' : HOME[me.role]);
} catch {
  location.replace('/login.html');
}
