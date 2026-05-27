/**
 * Telegram Bot API Helper
 */

const TELEGRAM_BOT_TOKEN = process.env.TELEGRAM_BOT_TOKEN;

interface SendMessageOptions {
  parse_mode?: 'MarkdownV2' | 'HTML';
  disable_notification?: boolean;
  reply_markup?: any;
}

/**
 * Sends a message to a Telegram chat.
 */
export async function sendTelegramMessage(
  chatId: string | number,
  text: string,
  options: SendMessageOptions & { botToken?: string } = {}
) {
  const token = options.botToken || TELEGRAM_BOT_TOKEN;
  if (!token) {
    console.warn('[Telegram] Skipping send: bot token is not set.');
    return null;
  }

  const url = `https://api.telegram.org/bot${token}/sendMessage`;

  try {
    const response = await fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        chat_id: chatId,
        text,
        parse_mode: options.parse_mode || 'MarkdownV2',
        disable_notification: options.disable_notification,
        reply_markup: options.reply_markup,
      }),
    });

    const data = await response.json();
    if (!data.ok) {
      console.error('[Telegram] Error sending message:', data.description);
    }
    return data;
  } catch (error) {
    console.error('[Telegram] Fetch error:', error);
    throw error;
  }
}

/**
 * Escapes characters for Telegram MarkdownV2.
 */
export function escapeMarkdown(text: string): string {
  return text.replace(/[_*[\]()~`>#+\-=|{}.!]/g, '\\$&');
}
