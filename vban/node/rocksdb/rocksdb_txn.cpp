#include <vban/node/rocksdb/rocksdb_txn.hpp>

vban::read_rocksdb_txn::read_rocksdb_txn (rocksdb::DB * db_a) :
	db (db_a)
{
	if (db_a)
	{
		options.snapshot = db_a->GetSnapshot ();
	}
}

vban::read_rocksdb_txn::~read_rocksdb_txn ()
{
	reset ();
}

void vban::read_rocksdb_txn::reset ()
{
	if (db)
	{
		db->ReleaseSnapshot (options.snapshot);
	}
}

void vban::read_rocksdb_txn::renew ()
{
	options.snapshot = db->GetSnapshot ();
}

void * vban::read_rocksdb_txn::get_handle () const
{
	return (void *)&options;
}

vban::write_rocksdb_txn::write_rocksdb_txn (rocksdb::OptimisticTransactionDB * db_a, std::vector<vban::tables> const & tables_requiring_locks_a, std::vector<vban::tables> const & tables_no_locks_a, std::unordered_map<vban::tables, vban::mutex> & mutexes_a) :
	db (db_a),
	tables_requiring_locks (tables_requiring_locks_a),
	tables_no_locks (tables_no_locks_a),
	mutexes (mutexes_a)
{
	lock ();
	rocksdb::OptimisticTransactionOptions txn_options;
	txn_options.set_snapshot = true;
	txn = db->BeginTransaction (rocksdb::WriteOptions (), txn_options);
}

vban::write_rocksdb_txn::~write_rocksdb_txn ()
{
	commit ();
	delete txn;
	unlock ();
}

void vban::write_rocksdb_txn::commit ()
{
	if (active)
	{
		auto status = txn->Commit ();

		// If there are no available memtables try again a few more times
		constexpr auto num_attempts = 10;
		auto attempt_num = 0;
		while (status.IsTryAgain () && attempt_num < num_attempts)
		{
			status = txn->Commit ();
			++attempt_num;
		}

		release_assert (status.ok (), status.ToString ());
		active = false;
	}
}

void vban::write_rocksdb_txn::renew ()
{
	rocksdb::OptimisticTransactionOptions txn_options;
	txn_options.set_snapshot = true;
	db->BeginTransaction (rocksdb::WriteOptions (), txn_options, txn);
	active = true;
}

void * vban::write_rocksdb_txn::get_handle () const
{
	return txn;
}

void vban::write_rocksdb_txn::lock ()
{
	for (auto table : tables_requiring_locks)
	{
		mutexes.at (table).lock ();
	}
}

void vban::write_rocksdb_txn::unlock ()
{
	for (auto table : tables_requiring_locks)
	{
		mutexes.at (table).unlock ();
	}
}

bool vban::write_rocksdb_txn::contains (vban::tables table_a) const
{
	return (std::find (tables_requiring_locks.begin (), tables_requiring_locks.end (), table_a) != tables_requiring_locks.end ()) || (std::find (tables_no_locks.begin (), tables_no_locks.end (), table_a) != tables_no_locks.end ());
}
