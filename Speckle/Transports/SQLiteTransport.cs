﻿using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Data.SQLite;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Timers;

namespace Speckle.Transports
{
  public class SqlLiteObjectTransport : IDisposable, ITransport
  {
    public string TransportName { get; set; } = "LocalTransport";

    public string RootPath { get; set; }

    public string ConnectionString { get; set; }

    private SQLiteConnection Connection { get; set; }

    private ConcurrentQueue<(string, string, int)> Queue = new ConcurrentQueue<(string, string, int)>();

    /// <summary>
    /// Timer that ensures queue is consumed if less than MAX_TRANSACTION_SIZE objects are being sent.
    /// </summary>
    private System.Timers.Timer WriteTimer;
    private int PollInterval = 500;

    private bool IS_WRITING = false;
    private int MAX_TRANSACTION_SIZE = 1000;

    public SqlLiteObjectTransport(string basePath = null, string applicationName = "Speckle", string scope = "Objects")
    {
      if (basePath == null)
        basePath = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);

      Directory.CreateDirectory(Path.Combine(basePath, applicationName)); //ensure dir is there

      RootPath = Path.Combine(basePath, applicationName, $"{scope}.db");
      ConnectionString = $@"URI=file:{RootPath};";

      Initialize();

      WriteTimer = new System.Timers.Timer() { AutoReset = true, Enabled = false, Interval = PollInterval };
      WriteTimer.Elapsed += WriteTimerElapsed;
    }

    private void Initialize()
    {

      // NOTE: used for creating partioned object tables.
      //string[] HexChars = new string[] { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c", "d", "e", "f" };
      //var cart = new List<string>();
      //foreach (var str in HexChars)
      //  foreach (var str2 in HexChars)
      //    cart.Add(str + str2);

      using (var c = new SQLiteConnection(ConnectionString))
      {
        c.Open();
        using (var command = new SQLiteCommand(c))
        {
          command.CommandText = @"
            CREATE TABLE IF NOT EXISTS objects(
              hash TEXT PRIMARY KEY,
              content TEXT
            ) WITHOUT ROWID;
          ";
          command.ExecuteNonQuery();
        }

        // Insert Optimisations

        SQLiteCommand cmd;
        cmd = new SQLiteCommand("PRAGMA journal_mode='wal';", c);
        cmd.ExecuteNonQuery();

        //Note / Hack: This setting has the potential to corrupt the db.
        //cmd = new SQLiteCommand("PRAGMA synchronous=OFF;", Connection);
        //cmd.ExecuteNonQuery();

        cmd = new SQLiteCommand("PRAGMA count_changes=OFF;", c);
        cmd.ExecuteNonQuery();

        cmd = new SQLiteCommand("PRAGMA temp_store=MEMORY;", c);
        cmd.ExecuteNonQuery();
      }
    }

    #region Writes

    /// <summary>
    /// Awaits untill write completion (ie, the current queue is fully consumed).
    /// </summary>
    /// <returns></returns>
    public async Task WriteComplete()
    {
      await Utilities.WaitUntil(() => { return GetWriteCompletionStatus(); }, 500);
    }

    /// <summary>
    /// Returns true if the current write queue is empty and comitted.
    /// </summary>
    /// <returns></returns>
    public bool GetWriteCompletionStatus()
    {
      Console.WriteLine($"write completion {Queue.Count == 0 && !IS_WRITING}");
      return Queue.Count == 0 && !IS_WRITING;
    }

    private void WriteTimerElapsed(object sender, ElapsedEventArgs e)
    {
      WriteTimer.Enabled = false;
      if (!IS_WRITING && Queue.Count != 0)
        ConsumeQueue();
    }

    private void ConsumeQueue()
    {
      IS_WRITING = true;
      var i = 0;
      ValueTuple<string, string, int> result;

      using (var c = new SQLiteConnection(ConnectionString))
      {
        c.Open();
        using (var t = c.BeginTransaction())
        {
          using (var command = new SQLiteCommand(c))
          {
            command.CommandText = $"INSERT OR IGNORE INTO objects(hash, content) VALUES(@hash, @content)";
            while (i < MAX_TRANSACTION_SIZE && Queue.TryPeek(out result))
            {
              Queue.TryDequeue(out result);
              command.Parameters.AddWithValue("@hash", result.Item1);
              command.Parameters.AddWithValue("@content", result.Item2);
              command.ExecuteNonQuery();
            }
            t.Commit();
          }
        }
      }

      if (Queue.Count > 0)
        ConsumeQueue();

      IS_WRITING = false;
    }

    /// <summary>
    /// Adds an object to the saving queue. 
    /// </summary>
    /// <param name="hash"></param>
    /// <param name="serializedObject"></param>
    public void SaveObject(string hash, string serializedObject)
    {
      Queue.Enqueue((hash, serializedObject, System.Text.Encoding.UTF8.GetByteCount(serializedObject)));

      WriteTimer.Enabled = true;
      WriteTimer.Start();
    }

    /// <summary>
    /// Directly saves the object in the db.
    /// </summary>
    /// <param name="hash"></param>
    /// <param name="serializedObject"></param>
    public void SaveObjectSync(string hash, string serializedObject)
    {
      using (var c = new SQLiteConnection(ConnectionString))
      {
        c.Open();
        using (var command = new SQLiteCommand(c))
        {
          command.CommandText = $"INSERT OR IGNORE INTO objects(hash, content) VALUES(@hash, @content)";
          command.Parameters.AddWithValue("@hash", hash);
          command.Parameters.AddWithValue("@content", serializedObject);
          command.ExecuteNonQuery();
        }
      }
    }

    #endregion

    #region Reads

    /// <summary>
    /// Gets an object.
    /// </summary>
    /// <param name="hash"></param>
    /// <returns></returns>
    public string GetObject(string hash)
    {
      using (var c = new SQLiteConnection(ConnectionString))
      {
        c.Open();
        using (var command = new SQLiteCommand(c))
        {
          command.CommandText = "SELECT * FROM objects WHERE hash = @hash LIMIT 1 ";
          command.Parameters.AddWithValue("@hash", hash);
          using (var reader = command.ExecuteReader())
          {
            while (reader.Read())
            {
              return reader.GetString(1);
            }
          }
        }
      }
      return null; // pass on the duty of null checks to consumers
    }

    #endregion

    /// <summary>
    /// Returns all the objects in the store. Note: do not use for large collections.
    /// </summary>
    /// <returns></returns>
    internal IEnumerable<string> GetAllObjects()
    {
      using var c = new SQLiteConnection(ConnectionString);
      c.Open();

      using var command = new SQLiteCommand(c);
      command.CommandText = "SELECT * FROM objects";

      using var reader = command.ExecuteReader();
      while (reader.Read())
      {
        yield return reader.GetString(1);
      }
    }

    /// <summary>
    /// Deletes an object. Note: do not use for any speckle object transport, as it will corrupt the database.
    /// </summary>
    /// <param name="hash"></param>
    internal void DeleteObject(string hash)
    {
      using (var c = new SQLiteConnection(ConnectionString))
      {
        c.Open();
        using (var command = new SQLiteCommand(c))
        {
          command.CommandText = "DELETE FROM objects WHERE hash = @hash";
          command.Parameters.AddWithValue("@hash", hash);
          command.ExecuteNonQuery();
        }
      }
    }

    public void Dispose()
    {
      // TODO: Check if it's still writing?
      Connection.Close();
      Connection.Dispose();
    }
  }
}
